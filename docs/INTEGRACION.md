# Integración

Objetivo: `KingOfFighters98UM_x64.exe`, image base `0x140000000`, timestamp `0x62033053`, `SizeOfImage` `0x1B31000`. Si cualquiera de esos valores o la firma de modo no coinciden, no se escribe ningún salto.

## Modo

`mov dword` en RVA `0x2B15EC`, bytes `C7 05 CE 7A 29 00 02 00 00 00`, destino RVA `0x5490C4`. PRACTICE es 6, el mismo valor que usa KofVsSelector. Cualquier otro valor, incluido el -1 leído al arrancar, deja los botones como llegaron y apaga grabación, reproducción y reversals.

## Reloj

RVA `0x5AE30`. Prólogo de 20 bytes verificado en disco y en el proceso. Se sustituye por un `jmp` indirecto de 14 bytes; el prólogo original vive en un trampolín. `game_on_frame` no conserva el trabajo de esa función: `rax` se guarda y se restaura. En el arranque de prueba el contador avanzó de 300 en 300 mientras el proceso seguía vivo. No se midió si esa llamada es exactamente un frame de simulación.

## Entrada

RVA `0x5B240`, `ecx` = índice 1..8, resultado en `eax`. Máscaras de fábrica, las mismas que `layout.c` del selector y la tabla `0x406910`: A `0x80`, B `0x20`, C `0x100`, D `0x40`. La dirección va en el nibble bajo y no se interpreta hasta que el usuario la calibra.

En el proceso de prueba, los primeros bytes de `0x5B240` eran `E9` hacia una reserva anónima. `dinput8.dll` de la carpeta es King of Moves. Ese salto no se pisa.

El sustituto es la llamada de 5 bytes en RVA `0x2B0B92` (`E8 A9 A6 DA FF`), dentro del bucle:

- `ecx = edi`, empezando en 1
- `call 0x5B240`
- `mov [rsi], eax` con `rsi = objeto+0x44`
- hasta el índice 8

Esa llamada se desvía a una página reservada a menos de 2 GB del ejecutable. La página llama a `0x5B240` (y por tanto al salto que ya estaba) y después a `game_on_mapped_input`. Si los 5 bytes no coinciden, se retira también el enganche de frame.

Cada índice se aplica una vez por valor del contador de frames. Una segunda llamada en el mismo frame recibe la misma palabra ya decidida.

## Carga

`Kof98Training.dll` la carga el `xinput1_4.dll` del selector, después de `Kof98Color.dll`. Si el archivo no está, el loader solo anota una línea. `DllMain` crea un hilo, espera 400 ms y entonces comprueba el ejecutable.

## Fuera de alcance hasta tener campos medidos

No se leen ni se escriben vida, aturdimiento, cajas ni posición. Un reversal con `known = 0` no arma nada. La reproducción de un slot y la prueba de A durante 2 frames sí sustituyen la palabra del índice dummy, y solo con modo 6.
