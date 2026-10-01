Selection lists now scroll a focused row sideways when it does not fit the row
box, so a long download URL (Firmware Update > Network > Select URL) can be read
instead of being cut off at the screen edge. The row moves four times per second
until the whole item has passed, then starts over from its head (`https://...`),
with the step sized so one loop takes about 10 seconds no matter how long the URL
is. Rows and items that already fit are drawn exactly as before, and scrolling
restarts as soon as the encoder moves the focus.
