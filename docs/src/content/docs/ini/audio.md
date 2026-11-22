---
title: "AudioEvent"
description: "New audio event options."
status: verified
verifiedAt: 7cdd826c5
sources:
  - Core/GameEngine/Source/Common/INI/INIAudioEventInfo.cpp
  - Core/GameEngine/Include/Common/AudioEventInfo.h
  - Core/GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp
sidebar:
  order: 33
---

(SoundEffects.ini and Voice.ini)

## Stop loops early

Added a `stopearly` flag for `Control =`, to be used for looping audio.
When the event is stopped (e.g. the weapon stops firing), the sound that is playing is cut off at once instead of being finished, and the `Decay` sound is played right after it. This allows using longer sound files without them playing on when their event is over already.

**example:**
```ini
AudioEvent RandomAmbientSoundLoop
  Control = loop random stopearly interrupt
  Sounds =  RandomLoopA
  Attack =  RandomLoopStartA
  Decay =  RandomLoopEndA
  ...
End
```
i.e. when using this audio for a looping weapon, whenever the weapon stops firing while in 'Attack' or normal stage, it jumps straight to 'Decay' (or stops the sound, if no Decay sounds are defined).

This has no effect for non-loop sounds. It is recommended to use 'interrupt' along with it.

Notes:
* It only works for positional (3D) sounds, i.e. sounds played at an object or position in the world. 2D sounds (e.g. UI sounds) and streamed sounds stop as before.
* The flag names for `Control` are `loop`, `random`, `all`, `postdelay`, `interrupt` and `stopearly` (case does not matter).
