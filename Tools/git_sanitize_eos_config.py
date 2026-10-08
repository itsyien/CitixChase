"""Git clean filter: retain runtime configuration while redacting EOS secrets."""
import re
import sys


def sanitize(data):
    return re.sub(
        rb"(?im)^([ \t]*(?:ClientSecret|client_secret)[ \t]*=[ \t]*)[^\r\n]*",
        rb"\1",
        data,
    )


if __name__ == "__main__":
    data = sys.stdin.buffer.read()
    sys.stdout.buffer.write(data if "--smudge" in sys.argv else sanitize(data))
