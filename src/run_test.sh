#!/bin/bash
echo "=== テスト実行開始 == "$(date)
./nebula_sanitizer.exe < test_cmd.txt > test_output.log 2>&1
echo "=== テスト終了: test_output.log にログを保存しました ==="
