# Changelog

## v1.0.5alpha (2025-07-10) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
- **Faster ROM emulation startup**: The code to initialize the ROM emulation has been moved before reading the local configuration parameters. This change allows the system to start up faster, and it avoids the annoying "bombs" that could occur when the ROM emulation was not ready before accessing the ROM code.
- **Display a text during the boot**: A text is displayed during the boot process to indicate that the system is starting up. This change improves the user experience by providing feedback during the boot process.
- **Faster write to the command buffer**: The code to write to the command buffer has been optimized, improving the performance of the system when sending commands.
- **Optimized microSD card configuration**: After testing different configurations, the microSD card configuration has been optimized to improve performance and reliability. This change enhances the overall user experience when accessing files on the microSD card.
- **Disabled USB mass storage after boot**: The USB mass storage feature has been disabled after the boot process. This change prevents potential conflicts and improves system stability. The USB mass storage only works during the setup menu. 

### New features
No new features so far.

### Fixes
Everything is a massive and ongoing fix...

---

## v1.0.4alpha (2025-06-17) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
No changes in the current features.

### New features
- **Support for Mega STE**: The Mega STE is now fully supported and tested in EmuTOS and TOS 2.06.

### Fixes
Everything is a massive and ongoing fix...

---

## v1.0.3alpha (2025-06-17) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
- **Re-enable framebuffer**: Now instead of directly writing to the screen memory, the framebuffer is used to manage screen updates. This change improves the performance and responsiveness of the display.

- **Faster timeout**: When there is an error due to timeout writing in the command buffer, the timeout has been reduced to a factor of 10. This change improves the responsiveness of the system specially when accesing the GEMDRIVE mode.

### New features
Real Time Clock (RTC) support has been added. The RTC is now available in the system, allowing for accurate timekeeping and date management.

### Fixes
Everything is a massive and ongoing fix...

---

## v1.0.2alpha (2025-06-10) - Alpha release

This is a rolling alpha release. Except new features, bugs and issues. No tracking yet of bugs and issues since there is no public release yet. This is a development version.

### Changes
- **ROM BUS stabilization when using the microSD**: The ROM BUS has been stabilized when using the microSD. This change improves the reliability of data transfer and access in the system. It should fix the problem of random bombs when entering into microSD access related menus.
- **Improved microSD access**: The microSD access has been improved, enhancing the overall performance and reliability of file operations.

### New features
No new features have been added in this release.

### Fixes
Everything is a massive and ongoing fix...

---

## v1.0.1alpha (2025-06-05) - Alpha release
- First version

---