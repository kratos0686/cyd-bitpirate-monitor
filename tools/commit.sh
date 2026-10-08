#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
git add -A
git commit -m "$1" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
