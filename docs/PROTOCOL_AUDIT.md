# E7-30 protocol audit notes

No protocol/programmer manual or confirmed byte capture was present in this repository.
The public type description confirms an RS-232/USB interface and a working-frequency
range of 25 Hz through 3 MHz, but it does not describe the serial packets.

The implementation therefore isolates the following behavior as an **unverified legacy
profile recovered from the old source code**:

- header `0xAA`;
- command numbers 67 (frequency), 71 (reset), and 72 (status);
- a 20-byte status response and two-byte acknowledgements;
- big-endian integer and IEEE-754 binary32 fields;
- flag positions and offsets of the two numeric fields;
- 9600 8-N-1 as the default serial configuration.

The old emulator wrote floats in host (little-endian on Windows) order while the parser
read them as big-endian. The emulator and parser now consistently use the isolated
big-endian profile, but that does not prove the real instrument's byte order.

The available material does not establish an embedded length field, checksum location or
algorithm, the units of the two numeric fields, or whether the second field is an angle in
radians. For that reason the database and UI call them `measured_value_1` and
`measured_value_2`; no radians-to-degrees conversion is performed.

Before real experimental registration, obtain the serial-interface section of
`УШЯИ.411218.021 РЭ` (or a manufacturer programmer manual) and verify the profile.
Also record known-good request/response captures for every command and mode. Update
`ProtocolProfile`, parser offsets/units, expected lengths, and checksum validation only
from that evidence.

Public references used only for non-packet capabilities:

- https://all-pribors.ru/docs/2017-69676-17.pdf
- https://oei-analitika.ru/kurilka/reestr_good_docs_page.php?id_mp=11836
