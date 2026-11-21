---
title: "AudioEvent"
description: "New audio event options."
status: unverified
sidebar:
  order: 33
---

(SoundEffects.ini and Voice.ini)

## Stop loops early

Added a `stopearly` control flag to be used for looping audio.
This causes the currently played audio to stop, whenever the loop ends, instead of finishing it.
Decay sounds will still be played afterwards. This allows using longer sound files without them playing when their event is over already.

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
