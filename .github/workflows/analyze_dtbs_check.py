#!/usr/bin/python3

import sys


IGNORE_ERRORS = [
    '\'fb-panel\' does not match any of the regexes',
    '\'framebuffer-panel\' does not match any of the regexes',
]

def is_error_ignored(line: str) -> bool:
    global IGNORE_ERRORS
    for ie in IGNORE_ERRORS:
        if ie in line:
            return True
    return False

def short_dtb_path(line: str) -> str:
    # /home/lexx/dev/kernels/linux/build-660/arch/arm64/boot/dts/qcom/sda660-xiaomi-clover.dtb: /: qcom,board-id: False schema does not allow [[11, 0]]
    # sda660-xiaomi-clover.dtb: /: qcom,board-id: False schema does not allow [[11, 0]]
    spos = line.find('.dtb:')
    if spos == -1:
        return line
    parts = line.split('.dtb:')
    dtb_part = parts[0]
    errmsg = parts[1]
    dtb_part = dtb_part.split('/')[-1]
    return dtb_part + '.dtb:' + errmsg

def main() -> int:
    if len(sys.argv) < 1:
        print('Expected results file as an argument')
        return 1

    results_filename = sys.argv[1]

    errors_count = 0
    errors_ignored = 0

    error_texts = []

    try:
        with open(results_filename, 'rt', encoding='utf-8') as f:
            for line in f.readlines():
                line = line.strip()
                if 'from schema $id' in line:
                    continue
                if line.startswith('SCHEMA ') or line.startswith('DTC [C]'):
                    continue
                errors_count = errors_count + 1
                if is_error_ignored(line):
                    errors_ignored = errors_ignored + 1
                    continue
                error_texts.append(short_dtb_path(line))
    except IOError as ex:
        print(str(ex))
        errors_count = 1

    errors_left = errors_count - errors_ignored
    if errors_left > 0:
        for l in error_texts:
            print(l)
    print(f'Errors: {errors_left} ({errors_count} total, {errors_ignored} ignored)')
    return errors_left


if __name__ == '__main__':
    sys.exit(main())
