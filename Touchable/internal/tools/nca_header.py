"""Restore the original header of an NCA that a cartridge-to-NSP conversion changed.

Format reference: https://switchbrew.org/wiki/NCA#Encryption
Converters change the distribution type from GameCard (1) to Download (0). This byte is in
header sector 1, so the NCA hash no longer matches its content ID. Restoring that byte must
reproduce the original hash exactly. Any other change still fails the content ID check.
"""

import ctypes

SECTOR_SIZE = 0x200
_ENCRYPT, _DECRYPT = 0, 1
_crypto = ctypes.CDLL("/usr/lib/system/libcommonCrypto.dylib")
_crypto.CCCrypt.argtypes = [ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_size_t,
                            ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t,
                            ctypes.POINTER(ctypes.c_size_t)]
_crypto.CCCrypt.restype = ctypes.c_int32


def _aes_ecb(operation, key, data):
    output, moved = ctypes.create_string_buffer(len(data)), ctypes.c_size_t()
    # kCCAlgorithmAES = 0, kCCOptionECBMode = 2.
    if _crypto.CCCrypt(operation, 0, 2, key, len(key), None, data, len(data), output, len(data), ctypes.byref(moved)) \
            or moved.value != len(data):
        raise ValueError("AES operation failed.")
    return output.raw


def xts_aes128(operation, key, data, tweak_value):
    """AES-128-XTS. Nintendo encodes the sector number as a big-endian tweak."""
    if len(key) != 32 or len(data) % 16:
        raise ValueError("XTS needs a 32-byte key and whole blocks.")
    tweak = bytearray(_aes_ecb(_ENCRYPT, key[16:], tweak_value.to_bytes(16, "big")))
    output = bytearray()
    for offset in range(0, len(data), 16):
        block = bytes(a ^ b for a, b in zip(data[offset:offset + 16], tweak))
        output += bytes(a ^ b for a, b in zip(_aes_ecb(operation, key[:16], block), tweak))
        carry = tweak[15] >> 7
        for index in range(15, 0, -1):
            tweak[index] = ((tweak[index] << 1) | (tweak[index - 1] >> 7)) & 0xFF
        tweak[0] = ((tweak[0] << 1) & 0xFF) ^ (0x87 if carry else 0)
    return bytes(output)


def gamecard_header(header, header_key):
    """Return the header with the GameCard distribution type, or None if it is not a converted NCA3 header."""
    if len(header) < 2 * SECTOR_SIZE:
        return None
    plain = bytearray(xts_aes128(_DECRYPT, header_key, header[SECTOR_SIZE:2 * SECTOR_SIZE], 1))
    if plain[:4] != b"NCA3" or plain[4] != 0:
        return None
    plain[4] = 1
    return header[:SECTOR_SIZE] + xts_aes128(_ENCRYPT, header_key, bytes(plain), 1) + header[2 * SECTOR_SIZE:]
