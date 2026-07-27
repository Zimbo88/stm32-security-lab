from __future__ import annotations

import struct
from collections.abc import Callable
from dataclasses import dataclass

from .errors import NackError, ProtocolError, ProtocolTimeout, Stm32CtlError
from .package import PackageInfo
from .protocol import (
    DEFAULT_MAX_WRITE_DATA_SIZE,
    HEADER_SIZE,
    ByteTransport,
    Command,
    Info,
    Response,
    Status,
    TargetStatus,
    parse_info,
    parse_response,
    parse_status,
    read_frame,
    status_name,
    write_frame,
)

DEFAULT_RESPONSE_TIMEOUT_SECONDS = 15.0
ProgressCallback = Callable[[int, int], None]


@dataclass(frozen=True)
class UpdateResult:
    image_version: int
    payload_size: int
    programmed_block_count: int
    final_status: TargetStatus


class Stm32Client:
    def __init__(
        self,
        transport: ByteTransport,
        *,
        timeout: float = DEFAULT_RESPONSE_TIMEOUT_SECONDS,
        retries: int = 1,
    ) -> None:
        self.transport = transport
        self.timeout = timeout
        self.retries = retries
        self.sequence = 0
        self.info: Info | None = None

    def _advance_sequence(self) -> None:
        self.sequence = (self.sequence + 1) & 0xFFFF

    def _send_once(self, command: Command, payload: bytes) -> Response:
        sequence = self.sequence
        write_frame(self.transport, command, sequence, payload)
        frame = read_frame(self.transport, self.timeout)
        response = parse_response(frame, sequence, command)
        self._advance_sequence()

        if response.response_command == Command.NACK:
            raise NackError(
                command=command,
                status=int(response.status),
                message=f"{command.name} rejected by target: {status_name(response.status)}",
            )
        if response.status != Status.OK:
            raise ProtocolError(f"{command.name} returned non-OK status: {response.status}")
        return response

    def request(
        self,
        command: Command,
        payload: bytes = b"",
        *,
        retry_idempotent: bool = False,
    ) -> Response:
        attempts = self.retries + 1 if retry_idempotent else 1
        last_timeout: ProtocolTimeout | None = None

        for attempt in range(attempts):
            try:
                return self._send_once(command, payload)
            except ProtocolTimeout as exc:
                last_timeout = exc
                if attempt + 1 >= attempts:
                    break

        if last_timeout is not None:
            raise last_timeout
        raise ProtocolError(f"{command.name} failed without a response")

    def hello(self) -> Info:
        response = self.request(Command.HELLO, retry_idempotent=True)
        info = parse_info(response.extra)
        self._validate_info(info)
        self.info = info
        return info

    def get_info(self) -> Info:
        response = self.request(Command.GET_INFO, retry_idempotent=True)
        info = parse_info(response.extra)
        self._validate_info(info)
        self.info = info
        return info

    def get_status(self) -> TargetStatus:
        response = self.request(Command.GET_STATUS, retry_idempotent=True)
        return parse_status(response.extra)

    def reset(self) -> None:
        self.request(Command.RESET)

    def abort_update(self) -> None:
        self.request(Command.ABORT_UPDATE)

    def update(
        self,
        package: PackageInfo,
        *,
        block_size: int | None = None,
        progress: ProgressCallback | None = None,
    ) -> UpdateResult:
        info = self.info if self.info is not None else self.hello()
        self._validate_package_against_target(package, info)

        max_write = info.max_write_data_size
        chunk_size = block_size if block_size is not None else max_write
        if chunk_size <= 0:
            raise ProtocolError("block size must be positive")
        if chunk_size > max_write:
            raise ProtocolError(f"block size {chunk_size} exceeds target maximum {max_write}")

        header = package.data[: package.signed_header_size]
        payload = package.data[package.signed_header_size :]
        if len(payload) != package.payload_size:
            raise ProtocolError("package payload size changed after validation")

        update_started = False
        try:
            self.request(Command.BEGIN_UPDATE, header)
            update_started = True

            offset = 0
            while offset < len(payload):
                chunk = payload[offset : offset + chunk_size]
                write_payload = struct.pack("<I", offset) + chunk
                self.request(Command.WRITE_BLOCK, write_payload)
                offset += len(chunk)
                if progress is not None:
                    progress(offset, len(payload))

            finish_response = self.request(Command.FINISH_UPDATE)
            final_status = parse_status(finish_response.extra)
            return UpdateResult(
                image_version=package.image_version,
                payload_size=package.payload_size,
                programmed_block_count=final_status.programmed_block_count,
                final_status=final_status,
            )
        except Stm32CtlError:
            if update_started:
                self._best_effort_abort()
            raise

    def _best_effort_abort(self) -> None:
        try:
            self.abort_update()
        except Stm32CtlError:
            pass

    @staticmethod
    def _validate_info(info: Info) -> None:
        if info.protocol_version != 1:
            raise ProtocolError(f"unsupported target protocol version: {info.protocol_version}")
        if info.header_size != HEADER_SIZE:
            raise ProtocolError(f"unexpected target header size: {info.header_size}")
        if info.max_write_data_size > DEFAULT_MAX_WRITE_DATA_SIZE:
            raise ProtocolError(
                f"target max write size exceeds host frame format: {info.max_write_data_size}"
            )
        if info.max_write_data_size == 0:
            raise ProtocolError("target reports zero write capacity")

    @staticmethod
    def _validate_package_against_target(package: PackageInfo, info: Info) -> None:
        if package.signed_header_size != 512:
            raise ProtocolError(f"unsupported package header size: {package.signed_header_size}")
        if package.package_size != package.signed_header_size + package.payload_size:
            raise ProtocolError("package size does not match header plus payload")
        if info.max_payload_size < package.signed_header_size:
            raise ProtocolError("target cannot receive update header in one BEGIN_UPDATE frame")
