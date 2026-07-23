EXIT_FAILED = 1
EXIT_TRANSPORT = 3
EXIT_TIMEOUT = 4
EXIT_PROTOCOL = 5
EXIT_NACK = 6
EXIT_PACKAGE = 7


class Stm32CtlError(Exception):
    exit_code = EXIT_FAILED


class TransportError(Stm32CtlError):
    exit_code = EXIT_TRANSPORT


class ProtocolError(Stm32CtlError):
    exit_code = EXIT_PROTOCOL


class ProtocolTimeout(ProtocolError):
    exit_code = EXIT_TIMEOUT


class CrcError(ProtocolError):
    pass


class NackError(ProtocolError):
    exit_code = EXIT_NACK

    def __init__(self, command: int, status: int, message: str) -> None:
        super().__init__(message)
        self.command = command
        self.status = status


class PackageValidationError(Stm32CtlError):
    exit_code = EXIT_PACKAGE
