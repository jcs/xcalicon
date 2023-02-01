# xcalicon

A small date and time widget for X11 which is intended to be iconified all the
time to remain on the desktop.
Its icon and window title indicate the current day and time.

![screenshot](https://user-images.githubusercontent.com/9888/216119384-ccd12d7e-f87c-426b-b9f6-de8cfd8b754e.png)

It was written to work with
[progman](https://github.com/jcs/progman)
but should work with any X11 window manager that handles `IconicState`
hints and shows icons in a useful manner.

## License

ISC

## Dependencies

`libX11` and `libXpm`

## Compiling

Fetch the source, `make` and then `make install`
