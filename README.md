# KOF '98 UMFE — entrenamiento

Mod de PRACTICE para **THE KING OF FIGHTERS '98 ULTIMATE MATCH FINAL EDITION** de Steam, ejecutable `KingOfFighters98UM_x64.exe` (app 222420, timestamp `0x62033053`).

El código está en `D:\KOF98-UMFE-Training`. No forma parte del mod de música.

## Instalación

1. Cierra el juego.
2. Ejecuta `build.bat` si vas a recompilar.
3. Ejecuta `install.bat`.

Eso hace tres cosas:

- Copia `xinput1_4.dll` actual a `xinput1_4.dll.kof98training-backup` si esa copia aún no existe y el archivo mide 6656 bytes.
- Sustituye el proxy del selector por uno que también carga `Kof98Training.dll` si el archivo está junto al ejecutable.
- Copia `Kof98Training.dll`.

No toca `dinput8.dll`. En esta instalación ese archivo es King of Moves (8 168 960 bytes) y se deja igual.

## Desinstalación

Cierra el juego y ejecuta `uninstall.bat`. Restaura el proxy desde la copia y borra `Kof98Training.dll`. `kof98training.ini` y `kof98training.log` se quedan; bórralos a mano si no los quieres.

## Uso

Entra a PRACTICE con el lado del dummy en **PLAYER**. Si está en CPU, la IA del juego también pulsa botones.

Abre el menú con **Ctrl+Shift+F10** o con **A+B+C+D** a la vez en el índice de menú (por defecto 1). F9 es de la pantalla de botones del juego, por eso no se usa. Mientras el menú está abierto, el juego recibe dirección y botones en cero para que el personaje no se mueva. Al cerrarlo, las asignaciones del juego y de Steam no se reescriben.

En el menú, el índice de menú manda:

- A baja de fila, B sube.
- D aumenta, C disminuye.
- C+D cierra.

Filas: wake-up, retraso, respuesta tras bloqueo, respuesta tras golpe, slot, peso, guardia, retraso extra aleatorio, índice del dummy, grabar, probar A / reproducir, calibrar direcciones, guardar.

El dummy por defecto es el índice 2 y el menú el índice 1. El primer arranque de esta versión escribió en el log los índices 1 a 8, así que esos dos valores son los que hay que corregir en el menú si el lado que se mueve no es el dummy.

Para un especial, un super o una evasión no hay lista de movimientos: graba la secuencia en un slot y elige la acción **grabacion**. La reproducción manual (fila «Probar boton A», botón C) no necesita el estado del personaje. Wake-up, block y hit sí lo necesitan, y hoy ese estado no está identificado.

Calibrar atrás / adelante / abajo guarda el nibble bajo que el juego ya entregó. No se asume qué bit es izquierda o derecha. Sin esa calibración, agarre y bloqueo se niegan y no se inyecta nada.

Guarda con la última fila. El archivo es `kof98training.ini`, al lado de la DLL.

## Qué quedó comprobado

`build.bat` compiló `TrainLogicTest.exe` y `Kof98Training.dll`. La prueba imprimió `logica ok`. Cubre, fuera del juego:

- wake-up con retraso 0, 1, 2, 3, 5 y 10, veinte repeticiones de cada uno en una línea de tiempo simulada;
- A en el primer frame legal después de que el blockstun llega a 0, y ninguna respuesta mientras el blockstun sigue;
- B al salir del hitstun, y wake-up en vez de hit reversal si hay derribo;
- grabación y reproducción en el mismo orden de frames;
- pesos 1 contra 3, cuarenta sorteos, todos resueltos y el peso 3 más frecuente;
- agarre sin dirección: no se inyecta.

Arranque real del `KingOfFighters98UM_x64.exe`, con King of Moves ya cargado como `dinput8.dll`. El proceso siguió vivo. `kof98training.log` quedó así (copia en `docs/prueba-carga.log`):

- el prólogo de `0x5B240` ya era un salto de otro módulo; no se reescribió;
- la entrada se enganchó en la llamada que rellena los índices 1..8;
- el reloj de frames avanzó (1, 301, 601, 901, 1201);
- el modo leído en el menú de arranque fue **-1**, no 6, y en ese estado el mod no sustituye botones.

## Qué no se comprobó dentro de una partida

No entré a PRACTICE. No derribé a Iori, no medí un blockstring, no grabé desde el fightstick y no abrí el menú con el Hori. Las pruebas A, B, C, D y E del encargo siguen pendientes. El modo 6 no apareció en este arranque.

Wake-up, block reversal y hit reversal no pueden ejecutarse todavía: no hay un campo verificado de knockdown, blockstun, hitstun ni «puede actuar». El código deja `estado=sin verificar` y no inventa el frame. La lógica de esos retrasos solo está probada con una línea de tiempo simulada.

Tampoco hay datos verificados para frame advantage, hitboxes, hurtboxes, throwboxes, esquinas, cambio de lado, vida, medidor ni counter hit. Esas filas no se dibujan con números supuestos. King of Moves ya muestra hitboxes en Practice con F2; este mod no lo reemplaza.

Steam Deck / Proton no se probó. La DLL es la misma y el proxy `xinput1_4` es el que esa cadena ya carga, pero no hubo una sesión en el Deck.
