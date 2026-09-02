# Regenerate with: vhs demo/vex-demo.tape   (run from the repo root, after
# `cmake --build build`). See README.md's "Error messages" section, which
# this demo mirrors with the exact same commands.

Output demo/vex-demo.gif

Set Shell "bash"
Set FontSize 20
Set Width 1360
Set Height 860
Set Padding 24
Set Theme "Dracula"
Set WindowBar Colorful
Set TypingSpeed 40ms

Hide
Type "export PS1='$ '" Enter
Type "clear" Enter
Show

Type "cat examples/fib.vx"
Enter
Sleep 2.5s

Type "./build/vex examples/fib.vx"
Enter
Sleep 2s

Type "./build/vex tests/errors/did_you_mean_variable.vx"
Enter
Sleep 3s

Type "./build/vex tests/type/type_errors.vx"
Enter
Sleep 3.5s

Type "./build/vex tests/errors/runtime_index_out_of_bounds.vx"
Enter
Sleep 3.5s
