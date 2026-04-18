import sys
args = sys.argv
errorCode = args[1]

if errorCode == '0':
    print("\033[32mProgram has run successfully.\033[39m")
else:
    hexError = hex(int(errorCode) & 0xffffffff)
    print("\033[31mProgram Crashed!!\033[39m")
    print(f'Program returned: {hexError} ({errorCode})')
    if hexError == "0xc06d007e":
        print("\033[36m[HINT] Make sure DLLs are enough to run.\033[39m")
    elif hexError == "0xc0000005":
        print("\033[36m[HINT] Access violation. DO NOT touch deleted objects!\033[39m")
    elif hexError == "0xc0000409":
        print("\033[36m[HINT] Buffer overrun... or you aborted Program.\033[39m")