Beta 5 fixes cursor artifacts when moving between the city and the widescreen HUD.

- Clears the complete native cursor at its exact sprite position, including
  the last row that previously left thin lines behind.
- Moves the whole 16-pixel hand with the right-hand HUD, including the part
  extending below it. The hand no longer splits across the screen.
- Draws the selected tool's original ROM outline at the host mouse endpoint
  during transitions, even while native OAM still contains the hand or hidden
  corners. Underlying city graphics, toolbar layers and other sprites remain.
- Adds actual RCI growth regressions for Normal, Big and Huge maps: eligible
  zones develop and gain population at X50; unpowered zones stay empty.

The reported stalled X50 block remains unconfirmed. A reproduction with 30
powered zones and no roads reached 11,360 residents after 6,000 game frames.
A road-connected test reached 10,080 at X50 versus 2,120 at Normal over the
same frame count. Development speed still respects native simulation visits,
growth conditions and pause state; it does not guarantee a population ratio.

Validated with renderer and mouse tests, native-ROM development, population,
power, construction, world and Journey tests, and recorded HUD/land cursor
transitions. Includes all Beta 4 enhancements. Windows x64 prerelease;
requires the user's clean US ROM, which is not included.
