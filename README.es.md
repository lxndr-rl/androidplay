# AndroidPlay

[English](README.md) · **Español**

Tu teléfono Android como **fuente** de CarPlay por cable: conéctalo a una radio con CarPlay por USB y la radio muestra una
pantalla de inicio de Android (Waze, Spotify, vídeo, mapas) con táctil y sonido, mientras la pantalla del teléfono
permanece apagada.

```
 teléfono (Android)                                radio del auto (CarPlay por cable)
 ┌──────────────────────────┐   USB-A ⇄ USB-C    ┌──────────────────────────┐
 │ pantalla virtual 1280×720│ ── vídeo H.264 ──▶ │  pantalla CarPlay        │
 │ launcher + tus apps      │ ── audio ────────▶ │  parlantes               │
 │ inyector táctil          │ ◀── táctil ─────── │  pantalla táctil         │
 └──────────────────────────┘   iAP2 + AirPlay   └──────────────────────────┘
```

Desarrollado y probado en un Samsung Galaxy S10+ (SM-G975F, LineageOS 23.2) con una radio Zlink5. Las partes que dependen
del equipo (cambio de rol USB, control del panel) están aisladas para poder añadir otros modelos.

## Lo principal

- **Verificado con la radio:** identificación y autenticación iAP2, sesión AirPlay sobre USB-NCM, vídeo H.264, audio y táctil.
- **Modo armado:** fuera de tu Wi‑Fi de casa el teléfono está listo *antes* de conectarlo; en casa es un teléfono ADB normal.
  Al conectarlo a un PC vuelve a ADB sin reiniciar. Se reconecta tras desconectar y conectar.
- **Pantalla de inicio para el auto:** clima, velocidad (GPS del teléfono), "Ahora suena", señal / Wi‑Fi / batería y una fila
  de apps, con textos de reemplazo cuando faltan datos. Gestos desde el borde: atrás, inicio, recientes.
- **Pantalla apagada mientras transmite**, y encendida al desconectar.

## Inicio rápido

```bash
git clone https://github.com/lxndr-rl/androidplay.git && cd androidplay
scripts/build-phone.sh                                   # compilar (hacen falta SDK y NDK)
scripts/deploy-phone.sh                                  # instalar en el teléfono (adb root)
scripts/install-autostart.sh                             # armarse solo en cada encendido (servicio init vía overlayfs)
```

Luego desconecta del PC, espera ~10 s y conecta a la radio. Pasos completos, requisitos, cómo revertir y solución de
problemas: **[docs/installation.es.md](docs/installation.es.md)** ([English](docs/installation.md)).

## Estado (2026-11-15)

| | |
|---|---|
| Verificado en hardware real | Zlink5 (Allwinner, v5.2.44), segundo puerto USB: sesión, vídeo, audio, táctil, reconexión, modo armado, vuelta a ADB en el PC. Evidencia: [docs/hardware-evidence.md](docs/hardware-evidence.md) (en inglés) |
| No verificado | cualquier otra radio (una tipo Chevrolet MyLink nunca llegó a una sesión); Waze / Maps / Spotify dentro de la pantalla proyectada; los gestos en la radio; botones multimedia (la Zlink solo expone un HID táctil) |
| Límites | requiere `adb root` (compilación userdebug); el autoarranque usa overlayfs de `adb remount` (probado solo en el S10+) |

## Repositorio

| Ruta | Qué es |
|---|---|
| `usb/GadgetManager` | compositor de gadget ConfigFS / FunctionFS, sonda de peticiones de control |
| `services/CarPlayService` | iAP2, cliente AirPlay, usbmux, ejecutor de sesión (C++, pruebas en PC) |
| `services/Projector`, `services/ProjectionService` | pantalla virtual, codificador H.264, política del panel, captura de audio |
| `services/InputBridge` | inyector táctil y filtro de gestos |
| `packages/CarLauncher` | pantalla de inicio, splash, cápsula Atrás / Inicio |
| `scripts/` | `build-phone.sh`, `deploy-phone.sh`, `run-tests.sh`, `device/androidplayd.sh` (gestor de modo en el teléfono) |
| `tests/hardware` | scripts de banco y de prueba con la radio |
| `docs`, `research` | arquitectura, gestor de modo, evidencia, notas de protocolo, notas legales (en inglés) |

Pruebas que no necesitan hardware: `scripts/run-tests.sh`.

## Legal y seguridad

El CarPlay por cable exige la autenticación MFi de Apple; este proyecto **no** la evita ni incluye claves de Apple. Presentar
IDs USB de Apple es un experimento opcional para la radio del propio dueño, se rechaza salvo que se defina
`APPLE_ID_OK=own_radio_only`, y queda fuera de cualquier imagen de distribución (ver
[research/licensing.md](research/licensing.md) y `LEGAL-001` en [TASKS.md](TASKS.md)). "CarPlay", "iPhone", "Android" y
"Google Play" son marcas de sus dueños; este proyecto no está afiliado a Apple ni a Google. Nada de esto toca `/system`,
`/vendor` ni las particiones de arranque.
