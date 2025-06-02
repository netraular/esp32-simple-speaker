Simple speaker app using ESP32-S3-N16R8, MAX98357A, 20x20x5mm Round 8 Ohm 1W speaker and a micro SD card reader. The ESP32-S3-N16R8 does not have a built-in DAC but has I2S.

Max 98357A data: [Adafruit MAX98357 I2S Class-D Mono Amp](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts)

## Connections

### Components:
*   **ESP32-S3-N16R8 Dev Board**: Microcontroller.
*   **MAX98357A Breakout Board**: I2S Mono Amplifier.
*   **Micro SD Card Reader**: For SPI communication.
*   **Speaker**: 8 Ohm, 1W.

### MAX98357A Breakout Board to ESP32-S3:

*   **VIN (MAX98357A)**: Connect to **3.3V** from ESP32. (Your current setup).
    *   *Power Output Note*: With 3.3V into an 8Ω speaker, expect approx. 0.6W @ 1% THD or 0.8W @ 10% THD.
*   **GND (MAX98357A)**: Connect to **GND** on ESP32.
*   **DIN (Data In, MAX98357A)**: Connect to ESP32's **I2S Data Out pin (GPIO4)**.
*   **BCLK (Bit Clock, MAX98357A)**: Connect to ESP32's **I2S Bit Clock pin (GPIO5)**.
*   **LRCK or WS (Left/Right Clock or Word Select, MAX98357A)**: Connect to ESP32's **I2S Word Select pin (GPIO6)**.
*   **GAIN (MAX98357A)**:
    *   **Your current setup**: Connected to **VIN (3.3V)**. This sets the gain to **6dB**.
    *   *Other options (refer to datasheet)*:
        *   Floating (unconnected): 9dB (default on Adafruit breakout)
        *   To GND: 12dB
        *   100K resistor to GND: 15dB
        *   100K resistor to VIN: 3dB
*   **SD (Shutdown Mode / Channel Select, MAX98357A)**:
    *   **Your current setup**: **Unconnected**.
    *   The Adafruit breakout board has a 1MΩ pull-up resistor from its SD pin to VIN, and the MAX98357A chip has an internal 100kΩ pull-down resistor on its SD pin.
    *   When the SD pin on the breakout is **unconnected** and VIN is 3.3V:
        *   A voltage divider is formed: `V_SD = VIN * (100kΩ / (1MΩ + 100kΩ)) = 3.3V * (100/1100) ≈ 0.3V`.
        *   According to the datasheet, a voltage between 0.16V and 0.77V on the SD pin sets the output to **(Left + Right)/2 (Stereo Average)**. This is the active mode for your current setup.
    *   *Other common configurations for SD pin*:
        *   To get **Left Channel Only**: Connect the `SD` pin on the breakout to `VIN` (3.3V). This makes V_SD > 1.4V.
        *   To **Shutdown** the amplifier: Connect the `SD` pin on the breakout to `GND`. This makes V_SD < 0.16V.
*   **OUT+ / OUT- (or SPK+ / SPK-)**: Connect to your 8 Ohm 1W speaker.

### Micro SD Card Reader to ESP32-S3:

*   **VCC**: Connect to **3.3V** from ESP32.
*   **GND**: Connect to **GND** on ESP32.
*   **MISO (Master In, Slave Out)**: Connect to ESP32's **SPI MISO pin (GPIO13)**.
*   **MOSI (Master Out, Slave In)**: Connect to ESP32's **SPI MOSI pin (GPIO11)**.
*   **SCK or CLK (Serial Clock)**: Connect to ESP32's **SPI Clock pin (GPIO12)**.
*   **CS or SS (Chip Select or Slave Select)**: Connect to ESP32's **SPI CS pin (GPIO10)**.

## Pin Definitions in `main.c`:

Corresponds to the connections above.

*   **I2S (MAX98357A):**
    *   `I2S_DO_IO` (Data Out): `GPIO_NUM_4`
    *   `I2S_BCK_IO` (Bit Clock): `GPIO_NUM_5`
    *   `I2S_WS_IO` (Word Select/LRCK): `GPIO_NUM_6`
*   **SPI (Micro SD Card Reader - SPI2_HOST):**
    *   `PIN_NUM_MOSI`: `GPIO_NUM_11`
    *   `PIN_NUM_MISO`: `GPIO_NUM_13`
    *   `PIN_NUM_CLK`: `GPIO_NUM_12`
    *   `PIN_NUM_CS`: `GPIO_NUM_10`

## Wiring Diagram Sketch:
```
ESP32-S3                         MAX98357A
  3V3 ---------------------------- VIN
  GND ---------------------------- GND
  GPIO4 (I2S_DO_IO) -------------- DIN
  GPIO5 (I2S_BCK_IO) ------------- BCLK
  GPIO6 (I2S_WS_IO) -------------- LRCK
  (SD pin on breakout unconnected) -- Amplifier ON, Output: (Left+Right)/2 (Stereo Average)
  3.3V (VIN) --------------------- GAIN (Sets Gain to 6dB)
                                   OUT+ --- Speaker --- OUT-

ESP32-S3                         Micro SD Card Reader
  3V3 ---------------------------- VCC
  GND ---------------------------- GND
  GPIO11 (SPI_MOSI) -------------- MOSI
  GPIO13 (SPI_MISO) -------------- MISO
  GPIO12 (SPI_SCK) --------------- SCK
  GPIO10 (SPI_CS) ---------------- CS
```

## Notes:
*   The `main.c` code plays mono audio. Since your MAX98357A is configured for (Left+Right)/2 (Stereo Average), it will effectively play the mono signal. If your WAV files are stereo, they will be mixed down to mono by the amplifier. If they are mono, the left and right channels effectively carry the same data, so (L+R)/2 will still be the mono signal.
*   Ensure your WAV files are PCM format, 8-bit or 16-bit, as supported by the `play_wav` function.
