#!/bin/zsh
set -eu
cd -- "${0:A:h}"
if /usr/bin/curl -fsS --max-time 2 http://127.0.0.1:8000/ 2>/dev/null | /usr/bin/grep -F '<title>Теплица № 08</title>' >/dev/null; then
  exec /usr/bin/open http://127.0.0.1:8000/
fi
make -C firmware native
(
  for attempt in {1..30}; do
    if /usr/bin/curl -fsS --max-time 1 http://127.0.0.1:8000/ >/dev/null 2>&1; then
      /usr/bin/open http://127.0.0.1:8000/
      break
    fi
    sleep 0.3
  done
) &
exec .venv/bin/python app.py --demo
