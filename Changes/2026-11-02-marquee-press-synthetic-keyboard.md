- Run the rectangle and ellipse press self-test fixtures on the self-test's synthetic keyboard for their whole
  run, like the other standalone press fixtures, and hold the Shift and Alt scenarios' modifiers there, so a key
  pressed on the desktop during the hidden WARP self-test can no longer change the press mode at button-down.
  The native thread key state is saved and restored around the run; every assertion and expected value is
  unchanged.
