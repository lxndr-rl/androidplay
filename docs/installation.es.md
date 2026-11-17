# Guía de instalación

[English](installation.md) · **Español**

Cómo instalar AndroidPlay en un teléfono y conectarlo a una radio con CarPlay por cable. Es la guía de uso diario; el plan para
instalar LineageOS en el teléfono es otra página: [rom-install-plan.md](rom-install-plan.md) (en inglés).

> **Nota de honestidad.** Todo lo de abajo lo hizo el autor con un teléfono (Samsung Galaxy S10+, SM-G975F) y una radio
> (Zlink5). Los pasos marcados *(probado)* funcionaron ahí. Cualquier otro teléfono u otra radio no está probado.

## 0. Qué necesitas

| | |
|---|---|
| **Teléfono** | Un teléfono Android con `adb root` y un controlador USB que actúe como host y como gadget (solo el probado se sabe que funciona). Probado: SM-G975F con LineageOS 23.2 nightly **userdebug**. Una compilación normal (user) no tiene `adb root` y los scripts se niegan a correr. El bootloader desbloqueado y la instalación de LineageOS quedan fuera de esta guía: ver [rom-install-plan.md](rom-install-plan.md). |
| **Radio** | Una radio con CarPlay *por cable* en un puerto USB-A. Probado: Zlink5 (Allwinner, v5.2.44) en su **segundo** puerto USB. |
| **Cable** | USB-A (radio) a USB-C (teléfono). Un cable de datos normal basta; no hace falta Power Delivery. |
| **PC (Linux)** | Android SDK (plataforma y build-tools: `aapt2`, `d8`, `zipalign`, `apksigner`), Android NDK r26+, JDK 17+, `adb`. `ffmpeg` solo sirve para ver capturas de prueba. |

## 1. Descarga el código y corre las pruebas

```bash
git clone https://github.com/lxndr-rl/androidplay.git && cd androidplay
scripts/run-tests.sh          # pruebas solo en el PC, sin teléfono; esperado: pass=7 fail=0
```

## 2. Compila todo para el teléfono

```bash
scripts/build-phone.sh        # herramientas nativas arm64, archivos dex y las dos APK -> .build/phone/
```

Si no encuentra el NDK, define `ANDROID_NDK_ROOT` (y `ANDROID_SDK_ROOT` para el SDK).

## 3. Instala en el teléfono

Conecta el teléfono al PC, activa la depuración USB, comprueba `adb devices` y ejecuta:

```bash
scripts/deploy-phone.sh
```

Copia las herramientas a `/data/local/tmp`, instala el launcher (`dev.lxndr.launcher`) y el servicio de proyección
(`dev.lxndr.projection`), y da al launcher permiso de ubicación, estado del teléfono y acceso a notificaciones
(tarjeta de velocidad, barras de señal, "Ahora suena"). **No** inicia nada que cambie el comportamiento USB.

## 4. Dile al teléfono dónde está "casa"

El teléfono se comporta como un teléfono ADB normal mientras esté conectado a tu Wi‑Fi de casa, y se *arma* para la radio en
cualquier otro sitio. El nombre de la red queda en un archivo del teléfono y nunca se sube al repositorio:

```bash
adb shell 'echo -n "NOMBRE_DE_TU_WIFI" > /data/local/tmp/androidplay.home_ssid'
```

Sin ese archivo el teléfono nunca se arma solo. Para forzar un estado: `touch /data/local/tmp/androidplay.car` (siempre
armado) o `androidplay.home` (siempre ADB).

## 5. Apps (opcional)

El launcher muestra lo instalado: apps de usuario más algunas del sistema. Waze, Google Maps y Spotify no vienen incluidas.
*(probado)* El autor las instaló con [Aurora Store](https://f-droid.org/packages/com.aurora.store/) (sesión anónima, APK de
F-Droid con hash y firma verificados). Sin Google Play Services, Maps puede funcionar limitado. Inicia sesión en cada app una
vez en la pantalla del teléfono antes de ir al auto. Para fijar u ocultar apps usa `config.properties` (`apps.pinned`,
`apps.hidden`, nombres de paquete separados por comas) en el directorio privado del launcher.

## 6. Inicia el gestor de modo USB

Presentar IDs USB de Apple es un **experimento opcional para tu propia radio**: el
gestor se niega a arrancar sin la variable explícita de abajo. Nunca forma parte de una imagen de distribución.

```bash
APPLE_ID_OK=own_radio_only scripts/deploy-phone.sh --start
adb shell /data/local/tmp/androidplayd.sh status        # state=adb ...
```

Sin autoarranque (sección siguiente) el gestor **no** corre tras reiniciar el teléfono y este es un teléfono ADB normal (el estado
seguro); ejecuta de nuevo el mismo comando. Interruptor de emergencia: `adb shell touch /data/local/tmp/androidplay.off`. Para parar y
restaurar la configuración USB original: `adb shell /data/local/tmp/androidplayd.sh stop`. Al parar se corta el enlace USB y puede matar un `adb shell` que lo esté ejecutando a medias (el gadget queda desmontado y el
teléfono fuera del bus hasta reiniciar): ejecútalo desacoplado, `adb shell 'nohup /data/local/tmp/androidplayd.sh stop >/dev/null 2>&1 &'`,
y si `adb devices` sale vacío desconecta y vuelve a conectar el cable una vez. Nunca dejes el gestor parado antes de un viaje: con el
gestor parado la radio ve un teléfono Android normal.

## 6b. Autoarranque en cada encendido (recomendado)

Un gestor que muere con cada reinicio significa un viaje perdido: la radio ve un teléfono Android normal. Esto instala un servicio
de `init` para que el teléfono se arme solo, sin PC:

```bash
scripts/install-autostart.sh          # un reinicio automático al final; se deshace con --remove
```

Cómo funciona *(probado)*: `adb remount` pone un overlayfs sobre `/vendor` (no se escriben las particiones reales; los cambios
viven en un área aparte), y `/vendor/etc/init/androidplay.rc` arranca el gestor tras `sys.boot_completed`, reiniciándolo si
muere. El `.rc` lleva el consentimiento `APPLE_ID_OK=own_radio_only` de la sección 6: bórralo con `--remove` para revocarlo. Nunca
bloquea el arranque y el interruptor `touch /data/local/tmp/androidplay.off` sigue funcionando. Tras el reinicio
`adb shell /data/local/tmp/androidplayd.sh status` debe responder `state=adb` por sí solo. Requiere una compilación userdebug con
bootloader desbloqueado. No probado en otros teléfonos.

## 7. Conecta a la radio *(probado en Zlink5)*

1. Desconecta el teléfono del PC. Espera unos 10 segundos: fuera de tu Wi‑Fi de casa se arma solo.
2. Conéctalo al puerto USB de la radio. En la Zlink5 el que funciona es el **segundo** puerto; si la radio dice
   "Link fail, try another USB port", usa el otro.
3. Esperado: "Connecting", luego el splash de AndroidPlay, luego el launcher, con táctil y sonido. La pantalla del teléfono
   se apaga mientras transmite.
4. Desconecta y conecta otra vez: se reconecta.
5. De vuelta al PC: conéctalo; `adb` vuelve sin reiniciar. Si no vuelve, reconecta el cable una vez.

Gestos en la pantalla proyectada *(implementados, aún no probados en la radio)*: deslizar desde el borde izquierdo o derecho hacia dentro = Atrás, deslizar hacia arriba
desde el borde inferior = Inicio, deslizamiento largo hacia arriba = Recientes. La cápsula Atrás / Inicio también está en
pantalla.

Micrófono de la radio *(experimental, apagado por defecto)*: el micrófono de la radio puede ser el micrófono del teléfono para todas las apps (comandos de
voz de Waze, búsqueda por voz). El lado Android está verificado en el teléfono con un tono de prueba; aún no se sabe si la radio envía audio del micrófono.
Para probarlo antes de un viaje: `adb shell touch /data/local/tmp/androidplay.mic`, conecta a la radio y habla mientras una app escucha. `airplay.log`
mostrará `mic: first packet` (o ninguna línea de mic, lo que significa que esta radio no lo envía) y `projector.log` mostrará `radio microphone attached`.
Para apagarlo: `adb shell rm /data/local/tmp/androidplay.mic`. Si la radio rechaza la solicitud extra, la sesión sigue sin el micrófono.

Ventanas seguras *(apagado por defecto)*: algunas pantallas (el inicio de sesión de Spotify, apps bancarias) las marca como seguras su app y se ven
negras en la radio. Para mostrarlas, marca la pantalla proyectada como segura: `adb shell touch /data/local/tmp/androidplay.secure` antes de conectar
(borra el archivo para apagarlo). Verificado solo sin radio.

## Desinstalar / revertir

```bash
adb shell /data/local/tmp/androidplayd.sh stop
adb uninstall dev.lxndr.launcher; adb uninstall dev.lxndr.projection
adb shell 'rm -f /data/local/tmp/androidplay* /data/local/tmp/{gadget_manager,ffs_probe,mdns_probe,carplay_phone,usb_host_iap2,projector.dex,inject.dex,swap_to_host_hook.sh}'
```

Nada de esto toca `/system`, `/vendor` ni las particiones de arranque; reiniciar devuelve el teléfono al USB original.

## Solución de problemas

| Síntoma | Qué probar |
|---|---|
| `adb devices` sale vacío tras `stop` o `start` | Desconecta y vuelve a conectar el cable una vez (el puerto del PC puede bloquearse tras mostrar IDs de Apple). Reiniciar (Vol. abajo + Power ~10 s) siempre funciona. |
| `refused: set APPLE_ID_OK=own_radio_only` | Es lo esperado; lee la sección 6 y define la variable. |
| La radio muestra "Link fail" | Puerto USB equivocado. Prueba el otro. |
| La radio se queda en "Connecting" | El teléfono no estaba armado al conectar. Comprueba que `status` diga `CARPLAY` (armado), que estés fuera del Wi‑Fi de casa (o que exista `androidplay.car`), y espera 10 s tras desconectar del PC. |
| El clima dice "Sin conexión" | No hay internet o ubicación todavía. Se llena cuando haya GPS y datos. |
| La velocidad muestra "--" | Aún no hay fix de GPS (cielo abierto). |
| Logs para reportar un error | `adb pull /data/local/tmp/androidplayd.log`, `androidplayd.ctrl.log`, `swap.log`, `airplay.log`, `iap.log`, `projector.log` |

Más: [troubleshooting.md](troubleshooting.md), [mode-manager.md](mode-manager.md), [hardware-evidence.md](hardware-evidence.md) (en inglés).

## Límites conocidos

- Probado con un teléfono y una radio. Una radio tipo Chevrolet MyLink llegó al primer saludo USB pero nunca a una sesión.
- El autoarranque depende del soporte de overlayfs de `adb remount` (probado solo en el S10+); sin él el gestor se inicia a mano tras reiniciar.
- Waze / Maps / Spotify dentro de la pantalla proyectada no están probados.
