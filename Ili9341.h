// Minimal ILI9341 (240x320 SPI TFT) driver for the Daisy Seed / Daisy Studio.
//
// Copied from menu/firmware (display-test) for the Triptech menu UI. libDaisy
// ships OLED and HD44780 drivers but nothing for the ILI9341 colour TFT used on
// the `menu` control surface, so this is a small self-contained driver: enough
// to set an address window and blit RGB565 pixels.
//
// Wiring (see menu/README.md and the menu schematic):
//   SPI1: SCK=D8, MOSI=D10, CS/NSS=D7 (hardware NSS)
//   DC/RS = D20, LED backlight = D17, RESET tied high (software reset only).
//
// 4-wire SPI, mode 0. MISO (D9) is unused: we never read the panel back.
//
// This Triptech copy extends the 5x7 font with a few extra glyphs ('/', '%',
// '+', '>') needed by the menu UI value formatters.

#pragma once

#include "daisy_seed.h"

class Ili9341 {
  public:
    // Panel is 240x320 in its native portrait orientation.
    static constexpr uint16_t kWidth = 240;
    static constexpr uint16_t kHeight = 320;

    // A handful of RGB565 colours for the demo.
    static constexpr uint16_t kBlack = 0x0000;
    static constexpr uint16_t kWhite = 0xFFFF;
    static constexpr uint16_t kRed = 0xF800;
    static constexpr uint16_t kGreen = 0x07E0;
    static constexpr uint16_t kBlue = 0x001F;
    static constexpr uint16_t kCyan = 0x07FF;
    static constexpr uint16_t kMagenta = 0xF81F;
    static constexpr uint16_t kYellow = 0xFFE0;
    static constexpr uint16_t kOrange = 0xFD20;
    static constexpr uint16_t kNavy = 0x000F;
    static constexpr uint16_t kGray = 0x8410;
    static constexpr uint16_t kDkGray = 0x39E7;

    void Init() {
        using namespace daisy;

        // --- DC/RS line (command vs data) -------------------------------
        GPIO::Config dc_cfg;
        dc_cfg.pin = seed::D20;
        dc_cfg.mode = GPIO::Mode::OUTPUT;
        dc_cfg.pull = GPIO::Pull::NOPULL;
        dc_cfg.speed = GPIO::Speed::VERY_HIGH;
        dc_.Init(dc_cfg);
        dc_.Write(true);

        // --- LED backlight: just drive it on for the test ---------------
        // (Production firmware should PWM this above ~20 kHz on TIM3_CH4 to
        // keep switching noise out of the audio band -- see README.)
        GPIO::Config bl_cfg;
        bl_cfg.pin = seed::D17;
        bl_cfg.mode = GPIO::Mode::OUTPUT;
        bl_cfg.pull = GPIO::Pull::NOPULL;
        bl_cfg.speed = GPIO::Speed::LOW;
        backlight_.Init(bl_cfg);
        backlight_.Write(true);

        // --- SPI1, master, TX-only, hardware NSS on D7 ------------------
        SpiHandle::Config scfg;
        scfg.periph = SpiHandle::Config::Peripheral::SPI_1;
        scfg.mode = SpiHandle::Config::Mode::MASTER;
        scfg.direction = SpiHandle::Config::Direction::TWO_LINES_TX_ONLY;
        scfg.datasize = 8;
        scfg.clock_polarity = SpiHandle::Config::ClockPolarity::LOW;
        scfg.clock_phase = SpiHandle::Config::ClockPhase::ONE_EDGE;
        scfg.nss = SpiHandle::Config::NSS::HARD_OUTPUT;
        scfg.baud_prescaler = SpiHandle::Config::BaudPrescaler::PS_8;
        scfg.pin_config.sclk = seed::D8;
        scfg.pin_config.mosi = seed::D10;
        scfg.pin_config.miso = seed::D9; // unused, but the periph wants a pin
        scfg.pin_config.nss = seed::D7;
        spi_.Init(scfg);

        InitPanel();
    }

    // Fill the whole screen with one colour.
    void FillScreen(uint16_t color) { FillRect(0, 0, kWidth, kHeight, color); }

    // Fill a rectangle. Clipped to the panel bounds.
    void FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        if (w <= 0 || h <= 0 || x >= kWidth || y >= kHeight)
            return;
        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (x + w > kWidth)
            w = kWidth - x;
        if (y + h > kHeight)
            h = kHeight - y;

        SetAddrWindow(x, y, x + w - 1, y + h - 1);

        // Fill a small staging buffer with the colour, then blit it in chunks.
        const uint8_t hi = color >> 8;
        const uint8_t lo = color & 0xFF;
        for (size_t i = 0; i < kChunkPixels; ++i) {
            chunk_[2 * i] = hi;
            chunk_[2 * i + 1] = lo;
        }

        uint32_t remaining = (uint32_t)w * (uint32_t)h;
        SetDc(true);
        while (remaining) {
            size_t n = remaining > kChunkPixels ? kChunkPixels : remaining;
            spi_.BlockingTransmit(chunk_, n * 2, 1000);
            remaining -= n;
        }
    }

    // 1px-thick rectangle outline.
    void DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        FillRect(x, y, w, 1, color);
        FillRect(x, y + h - 1, w, 1, color);
        FillRect(x, y, 1, h, color);
        FillRect(x + w - 1, y, 1, h, color);
    }

    // Draw a single character at (x,y) scaled by `size`, with a solid bg.
    void DrawChar(int16_t x, int16_t y, char c, uint16_t color, uint16_t bg, uint8_t size) {
        const uint8_t *g = Glyph(c);
        for (uint8_t col = 0; col < 5; ++col) {
            uint8_t bits = g[col];
            for (uint8_t row = 0; row < 7; ++row) {
                uint16_t px = (bits & (1 << row)) ? color : bg;
                FillRect(x + col * size, y + row * size, size, size, px);
            }
        }
        // One blank column of inter-character spacing.
        FillRect(x + 5 * size, y, size, 7 * size, bg);
    }

    // Draw a string. No wrapping; clipped at the right edge by FillRect.
    void DrawString(int16_t x, int16_t y, const char *s, uint16_t color, uint16_t bg,
                    uint8_t size) {
        int16_t cx = x;
        for (; *s; ++s) {
            DrawChar(cx, y, *s, color, bg, size);
            cx += 6 * size;
        }
    }

  private:
    // ILI9341 command set (only what we use).
    enum Cmd : uint8_t {
        SWRESET = 0x01,
        SLPOUT = 0x11,
        DISPON = 0x29,
        CASET = 0x2A,
        PASET = 0x2B,
        RAMWR = 0x2C,
        MADCTL = 0x36,
        PIXFMT = 0x3A,
    };

    void SetDc(bool data) { dc_.Write(data); }

    void WriteCommand(uint8_t cmd) {
        SetDc(false);
        spi_.BlockingTransmit(&cmd, 1, 100);
    }

    void WriteData(const uint8_t *data, size_t len) {
        SetDc(true);
        spi_.BlockingTransmit(const_cast<uint8_t *>(data), len, 1000);
    }

    void WriteData8(uint8_t d) { WriteData(&d, 1); }

    void SetAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
        uint8_t buf[4];

        WriteCommand(CASET);
        buf[0] = x0 >> 8;
        buf[1] = x0 & 0xFF;
        buf[2] = x1 >> 8;
        buf[3] = x1 & 0xFF;
        WriteData(buf, 4);

        WriteCommand(PASET);
        buf[0] = y0 >> 8;
        buf[1] = y0 & 0xFF;
        buf[2] = y1 >> 8;
        buf[3] = y1 & 0xFF;
        WriteData(buf, 4);

        WriteCommand(RAMWR);
    }

    void InitPanel() {
        // RESET is tied high on the board, so kick the panel with a software
        // reset and wait for it to come back.
        WriteCommand(SWRESET);
        daisy::System::Delay(150);

        WriteCommand(SLPOUT);
        daisy::System::Delay(120);

        // MADCTL: MX | BGR -> native portrait, correct colour order on the
        // LCDWIKI MSP2202 modules.
        WriteCommand(MADCTL);
        WriteData8(0x48);

        // 16 bits/pixel (RGB565).
        WriteCommand(PIXFMT);
        WriteData8(0x55);

        WriteCommand(DISPON);
        daisy::System::Delay(120);
    }

    // --- 5x7 font (column-major, bit0 = top row) ------------------------
    // Uppercase + digits + a few symbols. Anything unmapped renders blank.
    static const uint8_t *Glyph(char c) {
        struct G {
            char c;
            uint8_t cols[5];
        };
        static const G kFont[] = {
            {' ', {0x00, 0x00, 0x00, 0x00, 0x00}}, {'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
            {'-', {0x08, 0x08, 0x08, 0x08, 0x08}}, {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
            {':', {0x00, 0x36, 0x36, 0x00, 0x00}}, {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
            {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}}, {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
            {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}}, {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
            {'5', {0x27, 0x45, 0x45, 0x45, 0x39}}, {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
            {'7', {0x01, 0x71, 0x09, 0x05, 0x03}}, {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
            {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}}, {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
            {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}}, {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
            {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}}, {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
            {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}}, {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
            {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}}, {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
            {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}}, {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
            {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}}, {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
            {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}}, {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
            {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}}, {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
            {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}}, {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
            {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}}, {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
            {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}}, {'W', {0x7F, 0x20, 0x18, 0x20, 0x7F}},
            {'X', {0x63, 0x14, 0x08, 0x14, 0x63}}, {'Y', {0x03, 0x04, 0x78, 0x04, 0x03}},
            {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
            // --- Triptech menu-UI extras ---
            {'/', {0x20, 0x10, 0x08, 0x04, 0x02}}, {'%', {0x23, 0x13, 0x08, 0x64, 0x62}},
            {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}}, {'>', {0x41, 0x22, 0x14, 0x08, 0x00}},
        };
        static const uint8_t kBlank[5] = {0, 0, 0, 0, 0};
        for (const G &g : kFont)
            if (g.c == c)
                return g.cols;
        return kBlank;
    }

    daisy::SpiHandle spi_;
    daisy::GPIO dc_;
    daisy::GPIO backlight_;

    // Staging buffer for block fills (kChunkPixels * 2 bytes).
    static constexpr size_t kChunkPixels = 256;
    uint8_t chunk_[kChunkPixels * 2];
};
