import sys
args = sys.argv
errorCode = args[1]

if errorCode == '0':
    print("\033[32mProgram has run successfully.\033[39m")
else:
    hexError = hex(abs(int(errorCode)))
    print("\033[31mProgram Crashed!!\033[39m")
    print(f'Program returned: {hexError} ({errorCode})')
    if hexError == "0x3ffffecb":
        print("\033[31m[HINT] Make sure DLLs are enough to run.\033[39m")