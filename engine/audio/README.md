# Engine Audio (`engine/audio`)

## Responsibility
Audio device output, spatial 3D voice positioning, bus mixing, sound effect playback, and voice lifetime management.

## What It Does Not Do
- Contains no file-path parsing or asset decoding in the audio mixing thread.
- Does not interrupt voices abruptly across mode transitions.

## Collaborators
- Uses `engine/core` and `engine/world`.
- Controlled by `engine/framework` and `game`.

## Conventions
- Public headers: `engine/audio/include/audio/`
- Tests: `tests/unit/audio/`
