- Move the DxUi pin from the API revision 3 maintenance commit `a059e07c` to DxUi `main` `f2e328a0`
  (API revision 4, the 2 November 2026 delivery with its consumer upgrade guide), which also brings the
  keyboard cancel of pending menu hover timers (DX-2). Device recovery now flushes the old context after
  releasing the swap chain, since the new embedded host no longer does it and the next swap chain was refused
  with `E_ACCESSDENIED`. A superseded chrome preparation (`E_ABORT`) draws again instead of counting as a
  failed frame; the chrome controls declare their copy and move operations deleted, tree rows write the new
  badge field, and the prepared layer rows build their accessibility view by member. The prepared layer-row
  accessibility fixture services the owner's messages until DxUi's notification thread lets go of the rows.
  `test.ps1` passes in x64 Debug, Release and ASan Debug; ARM64 and DX-2's product acceptance remain open.
  Restore requires revision 4; `a059e07c` with this adapter reverted is the rollback.
