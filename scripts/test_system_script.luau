-- test_system_script: Workspace外(System直下)に置かれたスクリプトの実行テスト
if workspace ~= nil then
    print("[PASS] system-level script runs and sees the active workspace")
else
    print("[FAIL] system-level script has nil workspace global")
end

wait(0.1)
print("[PASS] system-level script resumed from wait()")
