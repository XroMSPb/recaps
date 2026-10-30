Recaps
------------------------------------

This simple program allows you to switch between languages using the mostly-unused CapsLock key.

Just press CapsLock and the language will change. You can use Alt-CapsLock if you still need the old CapsLock action - to write a lot of text in capital letters.

If you accidentally typed some text in the wrong layout, Ctrl-CapsLock should fix it.

If you have more than two languages installed, you can select the ones you want to cycle through if you right click the application's icon.

The tray icon shows the language the active window is using, so you can tell at a glance what typing will produce.

The menu on that icon also lets you pause Recaps when CapsLock has to be CapsLock for a while, start it with Windows,
and leave a particular application alone - handy for a game that uses CapsLock for something of its own.

Also it improves language switching using Alt-Shift combination: LeftAlt-LeftShift will always switch to the first language installed,
while RightAlt-RightShift will always switch to the second.

Enjoy!

Eli Golovinsky
http://www.gooli.org/


Revision history
------------------------------------
0.9	Brought up to date for Windows 10 and 11.
	The tray icon now shows the current language.
	The layout is also switched through the Text Services
	Framework, so it changes in Store and WinUI applications
	as well, and the selected text is found through UI
	Automation rather than guessed at.
	Fixed the keyboard hook being dropped by Windows, the
	tray icon disappearing when Explorer restarts, and
	several ways the clipboard could be left holding the
	wrong thing.
	Builds for 64-bit Windows, remembers its settings per
	keyboard layout instead of per language name, and keeps
	its own temporary data out of the clipboard history.
	The menu can pause Recaps, start it with Windows and
	keep it out of applications you name.

0.8 Added conversion of currently selected text.

0.7 Added LeftAlt-LeftShift and RightAlt-RightShift combinations.

0.6	Added conversion of text typed with the wrong layout 
	using Ctrl-CapsLock. Alt-CapsLock now changes the old 
	CapsLock mode.

0.5	Fixed selected languages configuration not being saved 
	in the registry.

0.4	Fixed language not being switched in some applications.

0.3	Added a tray icon, a menu and support for more than two 
	languages. Recaps now cycles between the languages selected
	in the menu.

0.2	Changed Shift-CapsLock to change language as well as CapsLock.
	Ctrl-CapsLock now changes the old CapsLock mode.

0.1	Initial release
