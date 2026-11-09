// USB CDC read-only transfer of completed SIMH tape images.
// The service is cooperative and runs only through the RP2MCU serial poll.
#pragma once
bool serialTapeExportActive();
void serialTapeExportEnter();
void serialTapeExportPoll();
void serialTapeExportInput(char byte);
