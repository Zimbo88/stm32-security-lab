#!/usr/bin/env bash
set -u

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
OUT="$ROOT/audit/repository-audit.txt"

cd "$ROOT"

{
    echo "# STM32 Security Lab Repository Audit"
    echo
    echo "Generated UTC: $(date -u +'%Y-%m-%dT%H:%M:%SZ')"
    echo "Repository root: $ROOT"
    echo

    echo "============================================================"
    echo "1. GIT STATUS"
    echo "============================================================"
    git status --short --branch 2>&1 || true
    echo

    echo "============================================================"
    echo "2. TOP-LEVEL STRUCTURE"
    echo "============================================================"
    find . \
      -maxdepth 2 \
      -mindepth 1 \
      -not -path './.git*' \
      -not -path './.venv*' \
      -not -path './hil-results*' \
      -printf '%y %p\n' \
      | sort
    echo

    echo "============================================================"
    echo "3. FILE COUNTS BY EXTENSION"
    echo "============================================================"
    find . \
      -type f \
      -not -path './.git/*' \
      -not -path './.venv*/*' \
      -not -path './hil-results/*' \
      -printf '%f\n' \
      | awk '
        {
          name=$0
          if (name !~ /\./) {
            ext="[no extension]"
          } else {
            sub(/^.*\./, "", name)
            ext=tolower(name)
          }
          count[ext]++
        }
        END {
          for (ext in count) {
            printf "%6d  %s\n", count[ext], ext
          }
        }
      ' \
      | sort -nr
    echo

    echo "============================================================"
    echo "4. DOCUMENTATION FILES"
    echo "============================================================"
    find . \
      -type f \
      \( \
        -iname '*.md' \
        -o -iname '*.rst' \
        -o -iname '*.txt' \
        -o -iname '*.adoc' \
        -o -iname '*.pdf' \
        -o -iname '*.docx' \
        -o -iname '*.odt' \
      \) \
      -not -path './.git/*' \
      -not -path './.venv*/*' \
      -not -path './hil-results/*' \
      | sort
    echo

    echo "============================================================"
    echo "5. IMAGE AND DIAGRAM FILES"
    echo "============================================================"
    find . \
      -type f \
      \( \
        -iname '*.png' \
        -o -iname '*.jpg' \
        -o -iname '*.jpeg' \
        -o -iname '*.webp' \
        -o -iname '*.svg' \
        -o -iname '*.gif' \
        -o -iname '*.drawio' \
        -o -iname '*.plantuml' \
        -o -iname '*.puml' \
        -o -iname '*.mmd' \
      \) \
      -not -path './.git/*' \
      -not -path './.venv*/*' \
      -not -path './hil-results/*' \
      | sort
    echo

    echo "============================================================"
    echo "6. ARCHIVE CONTENT"
    echo "============================================================"
    if [ -d archive ]; then
        find archive -type f -printf '%p\n' | sort
    else
        echo "No archive directory found."
    fi
    echo

    echo "============================================================"
    echo "7. BOARD IMAGE CANDIDATES"
    echo "============================================================"
    find . \
      -type f \
      \( \
        -iname '*board*' \
        -o -iname '*discovery*' \
        -o -iname '*stm32f429*' \
        -o -iname '*f429*' \
        -o -iname '*devkit*' \
      \) \
      -not -path './.git/*' \
      -not -path './.venv*/*' \
      -not -path './hil-results/*' \
      | sort
    echo

    echo "============================================================"
    echo "8. LICENSE AND POLICY FILES"
    echo "============================================================"
    find . \
      -maxdepth 3 \
      -type f \
      \( \
        -iname 'license*' \
        -o -iname 'copying*' \
        -o -iname 'notice*' \
        -o -iname 'security.md' \
        -o -iname 'contributing.md' \
        -o -iname 'code_of_conduct.md' \
        -o -iname 'changelog.md' \
        -o -iname 'citation.cff' \
      \) \
      -not -path './.git/*' \
      | sort
    echo

    echo "============================================================"
    echo "9. README FILES"
    echo "============================================================"
    find . \
      -type f \
      -iname 'readme*' \
      -not -path './.git/*' \
      -not -path './.venv*/*' \
      -not -path './hil-results/*' \
      | sort
    echo

    echo "============================================================"
    echo "10. PRIVATE INFORMATION CANDIDATES"
    echo "============================================================"
    rg -n -i \
      --hidden \
      --glob '!.git/**' \
      --glob '!.venv*/**' \
      --glob '!hil-results/**' \
      --glob '!audit/repository-audit.txt' \
      --glob '!audit/run_repository_audit.sh' \
      '(mathias-zimmermann|MS-7D32|/home/[A-Za-z0-9._-]+|file:///home/|ttyUSB[0-9]+|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})' \
      . 2>&1 || true
    echo

    echo "============================================================"
    echo "11. LOCAL ABSOLUTE PATHS"
    echo "============================================================"
    rg -n \
      --hidden \
      --glob '!.git/**' \
      --glob '!.venv*/**' \
      --glob '!hil-results/**' \
      --glob '!audit/repository-audit.txt' \
      --glob '!audit/run_repository_audit.sh' \
      '(/home/|/Users/|[A-Z]:\\\\Users\\\\)' \
      . 2>&1 || true
    echo

    echo "============================================================"
    echo "12. POSSIBLE GERMAN TEXT"
    echo "============================================================"
    rg -n -i \
      --hidden \
      --glob '!.git/**' \
      --glob '!.venv*/**' \
      --glob '!hil-results/**' \
      --glob '!audit/repository-audit.txt' \
      --glob '!audit/run_repository_audit.sh' \
      --glob '*.md' \
      --glob '*.txt' \
      --glob '*.rst' \
      --glob '*.adoc' \
      --glob '*.py' \
      --glob '*.c' \
      --glob '*.h' \
      --glob '*.sh' \
      '\b(und|oder|aber|nicht|für|wird|wurde|werden|diese|dieser|dieses|sicher|sicherheit|bootloader|firmware|entwicklungsboard|fehler|prüfung|testlauf|ausgabe|ergebnis|hinweis|achtung|beschreibung|verzeichnis|datei)\b' \
      . 2>&1 || true
    echo

    echo "============================================================"
    echo "13. TODO / FIXME / HACK / XXX"
    echo "============================================================"
    rg -n \
      --hidden \
      --glob '!.git/**' \
      --glob '!.venv*/**' \
      --glob '!hil-results/**' \
      --glob '!audit/repository-audit.txt' \
      --glob '!audit/run_repository_audit.sh' \
      '\b(TODO|FIXME|HACK|XXX)\b' \
      . 2>&1 || true
    echo

    echo "============================================================"
    echo "14. GENERATED AND BUILD ARTIFACT CANDIDATES"
    echo "============================================================"
    find . \
      -type f \
      \( \
        -iname '*.o' \
        -o -iname '*.a' \
        -o -iname '*.elf' \
        -o -iname '*.hex' \
        -o -iname '*.bin' \
        -o -iname '*.map' \
        -o -iname '*.d' \
        -o -iname '*.pyc' \
        -o -iname '*.log' \
        -o -iname '*.tmp' \
        -o -iname '*.bak' \
        -o -iname '*~' \
      \) \
      -not -path './.git/*' \
      -not -path './.venv*/*' \
      -not -path './hil-results/*' \
      | sort
    echo

    echo "============================================================"
    echo "15. LARGE FILES OVER 5 MIB"
    echo "============================================================"
    find . \
      -type f \
      -size +5M \
      -not -path './.git/*' \
      -not -path './.venv*/*' \
      -not -path './hil-results/*' \
      -printf '%s %p\n' \
      | sort -nr \
      | awk '{
          size=$1
          $1=""
          printf "%.2f MiB%s\n", size / 1048576, $0
        }'
    echo

    echo "============================================================"
    echo "16. SUSPICIOUS SECRET-LIKE CONTENT"
    echo "============================================================"
    rg -n -i \
      --hidden \
      --glob '!.git/**' \
      --glob '!.venv*/**' \
      --glob '!hil-results/**' \
      --glob '!audit/repository-audit.txt' \
      --glob '!audit/run_repository_audit.sh' \
      --glob '!*.bin' \
      --glob '!*.elf' \
      --glob '!*.pdf' \
      '(private[_ -]?key|secret[_ -]?key|api[_ -]?key|access[_ -]?token|password|passwd|BEGIN [A-Z ]*PRIVATE KEY|seed\s*=|mnemonic)' \
      . 2>&1 || true
    echo

    echo "============================================================"
    echo "17. GIT-TRACKED GENERATED FILES"
    echo "============================================================"
    git ls-files \
      | grep -E \
        '(^|/)(__pycache__|build|dist|hil-results|\.pytest_cache|\.ruff_cache|\.mypy_cache)/|(\.pyc|\.o|\.elf|\.map|\.log|~)$' \
      || true
    echo

    echo "============================================================"
    echo "18. GITIGNORE"
    echo "============================================================"
    if [ -f .gitignore ]; then
        cat .gitignore
    else
        echo "No .gitignore found."
    fi
    echo

    echo "============================================================"
    echo "19. RECENT COMMITS"
    echo "============================================================"
    git log \
      --date=short \
      --pretty=format:'%h  %ad  %an  %s' \
      -n 25 2>&1 || true
    echo

    echo "============================================================"
    echo "20. SUBMODULES"
    echo "============================================================"
    if [ -f .gitmodules ]; then
        cat .gitmodules
        git submodule status 2>&1 || true
    else
        echo "No Git submodules configured."
    fi
    echo

    echo "============================================================"
    echo "21. TEST AND TOOL CONFIGURATION"
    echo "============================================================"
    find . \
      -maxdepth 4 \
      -type f \
      \( \
        -iname 'pyproject.toml' \
        -o -iname 'setup.cfg' \
        -o -iname 'tox.ini' \
        -o -iname 'pytest.ini' \
        -o -iname 'requirements*.txt' \
        -o -iname 'requirements*.in' \
        -o -iname 'poetry.lock' \
        -o -iname 'uv.lock' \
        -o -iname 'Makefile' \
        -o -iname 'CMakeLists.txt' \
      \) \
      -not -path './.git/*' \
      -not -path './.venv*/*' \
      | sort
    echo

    echo "============================================================"
    echo "22. GITHUB CONFIGURATION"
    echo "============================================================"
    if [ -d .github ]; then
        find .github -type f -printf '%p\n' | sort
    else
        echo "No .github directory found."
    fi
    echo

    echo "============================================================"
    echo "23. TRACKED FILE COUNT"
    echo "============================================================"
    git ls-files | wc -l
    echo

    echo "============================================================"
    echo "24. COMPLETE TRACKED FILE LIST"
    echo "============================================================"
    git ls-files | sort
    echo
} > "$OUT"

printf 'Audit written to:\n%s\n' "$OUT"
printf '\nSummary:\n'
grep -E \
  '^(No |[0-9]+ |# STM32|Generated UTC:|Repository root:)' \
  "$OUT" \
  | head -n 30 || true
