Beta 6 fixes the moving cursor artifacts introduced in Beta 5.

- Reads the original tool-to-outline table as one byte per tool. The old
  word indexing selected unrelated sprite records, drawing blocks and
  fragments around the mouse, especially with Nuclear and other large tools.
  All 15 construction tools now use their correct original outline.
- Hides the host minimap marker when the native minimap is parked offscreen.
  This removes the floating white speck and invisible minimap mouse regions.

Verified with recorded Nuclear cursor movement at the Huge map's lower/right
edges, before/after rendered captures, full-pixel checks for all construction
tool outlines, hidden-minimap rendering/hit tests, and the renderer and mouse
test suites. Includes all Beta 5 enhancements. Windows x64 prerelease;
requires the user's clean US ROM, which is not included.
