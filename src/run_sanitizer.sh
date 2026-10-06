#!/bin/bash
cd "$(dirname "$0")"
exec setarch $(uname -m) -R ./nebula_sanitizer.exe