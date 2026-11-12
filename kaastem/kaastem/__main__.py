"""Command line:

    python -m kaastem mektebimizde xalqımızdıń        # words
    python -m kaastem -t "Bizler mektepke bardıq."    # text
    python -m kaastem -f text.txt                     # file
    python -m kaastem -e jazıladı                     # with the analysis
"""
import argparse
import sys

from . import Stemmer


def main(argv=None):
    ap = argparse.ArgumentParser(prog='kaastem', description='Karakalpak stemmer')
    ap.add_argument('words', nargs='*', help='words')
    ap.add_argument('-t', '--text', help='text')
    ap.add_argument('-f', '--file', help="text file (UTF-8); '-' reads standard input")
    ap.add_argument('-e', '--explain', action='store_true', help='show the segmentation and the rules applied')
    args = ap.parse_args(argv)
    st = Stemmer()
    items = list(args.words)
    if args.text:
        items += [tok for tok, _ in st.stem_text(args.text)]
    if args.file:
        f = sys.stdin if args.file == '-' else open(args.file, encoding='utf-8')
        items += [tok for tok, _ in st.stem_text(f.read())]
    if not items:
        ap.print_help()
        return 1
    for w in items:
        a = st.analyze(w)
        print(str(a) if args.explain else f'{w}\t{a.stem}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
