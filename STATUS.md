Duende — STATUS

Fecha: 2026-11-14
Versión objetivo: LineageOS 23.2 (Android 16), rama oficial verificada para beyond2lte
Dispositivo: Galaxy S10+ beyond2lte — modelo NO verificado (nunca hubo dispositivo en adb)
Rama LineageOS: lineage-23.2

SUBAGENTES
- ROM: investigación + scripts; build nunca ejecutado (host pequeño)
- CarPlay: iAP2 + AirPlay (cliente, secuenciador, runner, JNI) probados en host/emulador
- USB: GadgetManager probado contra un árbol configfs falso
- Multimedia: pantalla virtual+H.264, inyector táctil, captura de audio probados en emulador (shell uid)
- UX: launcher probado en emulador
- QA: run-tests.sh, diagnostics.sh, validation.md
- Research: OkcarOS, alternativas, licencias

PROGRESO VERIFICADO
1. scripts/run-tests.sh: 7/7 suites (CMake+ctest 11 tests con ASan/UBSan, GadgetManager 46 checks, launcher, inputbridge, sintaxis scripts).
2. Emulador API 36: launcher; vídeo vivo + audio sintético desde un proceso Android nativo (JNI) a un coche falso del host; touch/keyframe llegan a Java; captura H.264 válida (ffprobe/ffmpeg); toque inyectado cambia el stream; audio del sistema capturado (440 Hz).
3. Bugs reales encontrados por pruebas y corregidos: permisos FGS, INTERNET, excepción JNI pendiente, carrera del encoder.

COMPILACIONES
- ROM: no compilada. APKs/libs: compilados con SDK/NDK (x86_64 ejecutado; arm64 solo compilado).

PRUEBAS
- Aprobadas: ver docs/validation.md (nivel 1 y parte del nivel 2, solo emulador)
- Fallidas: ninguna pendiente conocida
- Pendientes: niveles 3, 4 y 5 completos; AC-01..AC-12 sin probar en destino

BLOQUEOS
1. Sin teléfono ni radio CarPlay.
2. Cambio de rol USB y VBUS (posible retroalimentación) sin resolver.
3. Privilegios de ROM para captura/inyección sin escribir.
4. Máquina de build: 39 GB RAM / 189 GB disco frente a 64 GB / 400 GB.
5. Protocolo basado en un solo documento de terceros medido en un solo coche.

RIESGOS
1. Identidad Apple (VID, "iPhone"): cuestión legal abierta (LEGAL-001).
2. Radios modernas pueden exigir cifrado/emparejamiento (no implementado).

SIGUIENTES ACCIONES
1. Conectar el S10+ por adb y ejecutar scripts/diagnostics.sh (gate 0 de docs/hardware-bringup.md).
2. Decidir ruta de ROM (nightly + capa, o build en otra máquina).
3. Capturar un iPhone real contra la radio (gate 3).
