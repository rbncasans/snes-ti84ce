# click the wawas para TI-84 Plus CE

Port no oficial, hecho por fans, del **modo clásico** de *click the wawas* para la TI-84 Plus CE / TI-84 Plus CE Python Edition.

## Créditos

- **Juego original:** *click the wawas*, de **aidenkp11** (Scratch / TurboWarp, publicado en Game Jolt e itch.io). El autor permite hacer remixes y ports del juego.
- **Port para calculadora:** Rubén y Claude.
- Información del juego sacada de la [wiki de click the wawas](https://click-the-wawas.fandom.com/wiki/Click_the_wawas_Wiki).

Todos los gráficos están redibujados con primitivas de `graphx`: no se usa ninguna imagen ni sonido del juego original.

## Cómo jugar

| Tecla | Acción |
|---|---|
| Flechas | Moverse / mover el cursor |
| `[2nd]` (o `[alpha]`) | Clic, disparar, atacar, saltar |
| `[enter]` | Aceptar |
| `[clear]` | Volver al menú |

En el menú principal:

- **JUGAR (modo clásico):** los 12 stages seguidos. Si pierdes sale el *GAME OVER* y puedes reintentar el stage o volver al menú.
- **MINIJUEGOS:** juega cualquiera de los 12 minijuegos suelto para practicar.
- **CRÉDITOS**

## Los 12 stages

1. **click the wawas!!** – haz clic en 35 wawas en 30 segundos.
2. **spike the wawas** – toca con la bola de pinchos a los 8 wawas que rebotan.
3. **shoot the wawa** – apunta con el cursor y dispara a los 6 wawas (20 s).
4. **we r in space!!!** – flota con inercia por el espacio y toca a todos los wawas.
5. **catch the wawas!!!** – atrapa 15 wawas que caen con la cesta.
6. **theyre taking revenge....** – los wawas te atacan; golpéalos con `[2nd]` (5 vidas).
7. **dont touch the red wawas!!!** – llega a la bandera sin tocar los wawas rojos.
8. **survive the dogs for 30 seconds** – esquiva a los perros durante 30 segundos.
9. **crush th wawas before they get to the end!!!!** – baja la prensa para aplastar 15 wawas.
10. **gameboy** – plataformas en verde Game Boy: salta y toca a todos los wawas.
11. **rob the wawa** – esquiva las luces, roba el wawa del pedestal y vuelve a la puerta esquivando el cañón.
12. **the wawas are taking revenge again!??!?!** – 18 wawas de 6 tipos y, al final, el jefe **Petrovich**.

Al terminar aparece la pantalla de *Congratulations, you won!*… y un cactus.

## Compilar

Hace falta el [CE C Toolchain](https://github.com/CE-Programming/toolchain):

```sh
cd clickthewawas
make
```

Se genera `bin/WAWAS.8xp`, que se pasa a la calculadora con TI Connect CE o TiLP. Necesita las librerías de C de la CE (`clibs.8xg`) instaladas en la calculadora.

GitHub Actions también lo compila en cada push (workflow *Compilar click the wawas*) y deja el `.8xp` como artefacto descargable.
