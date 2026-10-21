class GameError(Exception):
    """A game file cannot be processed safely or with the supported formats."""


def read_exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise GameError("Input is truncated.")
    return data
