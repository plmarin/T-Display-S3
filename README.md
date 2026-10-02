# XRP Ticker

Visor del precio de **XRP** para la **LilyGo T-Display S3** (ESP32-S3, pantalla 320x170).

Basado en [NerdMiner v2](https://github.com/BitMaker-hub/NerdMiner_v2) (licencia MIT). Se ha eliminado todo lo
relativo al minado de BTC y se reutiliza el portal WiFi, el guardado de configuración y el manejo de pantalla y botones.
La caja 3D original de NerdMiner para esta placa sigue en [3d_files](3d_files/).

## Pantallas

| # | Pantalla | Contenido |
|---|----------|-----------|
| 1 | Precio   | Precio actual en grande, variación 24h (verde/rojo), máximo y mínimo 24h |
| 2 | Gráfica  | Evolución de las últimas 24h (velas de 30 min) con el rango y el precio actual |
| 3 | Reloj    | Hora y fecha locales, con precio y variación en la cabecera |
| 4 | Calendario | Hora de Madrid (con segundos y cambio de horario automático), fecha y calendario del mes con el día actual marcado |
| 5 | Mercado  | Capitalización, volumen 24h, máx/mín, hora del último dato, señal WiFi e IP |

El punto de la esquina superior derecha indica el estado: **verde** datos al día, **ámbar** datos de hace más de 5 min,
**rojo** sin WiFi. Los puntos de la parte inferior indican la pantalla actual.

## Botones

| Botón | Acción |
|-------|--------|
| BOOT (GPIO0), pulsación | Apagar/encender pantalla |
| BOOT (GPIO0), doble pulsación | Girar la pantalla 180° |
| KEY (GPIO14), pulsación | Siguiente pantalla |
| KEY (GPIO14), mantener 5 s | Borrar configuración y WiFi, y reiniciar |
| KEY (GPIO14), mantener al arrancar | Abrir el portal de configuración |

## Configuración

En el primer arranque (o manteniendo KEY al encender) la placa crea la red WiFi **`XRPTickerAP`**
(clave **`XRPTicker`**). Conéctate y abre `192.168.4.1` para elegir tu WiFi y estos ajustes:

| Ajuste | Por defecto | Notas |
|--------|-------------|-------|
| Moneda | `usd` | Cualquier `vs_currency` de CoinGecko: `usd`, `eur`, `gbp`, `jpy`, `btc`... |
| Refresco del precio | 60 s | Mínimo 30 s, por el límite de la API gratuita |
| Cambiar de pantalla cada | 15 s | `0` desactiva el cambio automático |
| Zona horaria | `CET-1CEST,M3.5.0,M10.5.0/3` | Formato POSIX TZ, con cambio de horario automático. Canarias: `WET0WEST,M3.5.0/1,M10.5.0` |

Los ajustes se guardan en SPIFFS (`/xrp_config.json`) y las credenciales WiFi las guarda WiFiManager.

## Datos

Se usa la API pública de [CoinGecko](https://www.coingecko.com/en/api), que no necesita clave:

- `simple/price?ids=ripple`: precio, variación 24h, capitalización y volumen, en cada refresco.
- `coins/ripple/ohlc?days=1`: 48 velas de 30 minutos para la gráfica y el máx/mín, cada 10 minutos.

Si la API responde `429` (límite de peticiones), se espera 2 minutos antes de reintentar. Ante cualquier otro error se reintenta a los 30 s.

## Compilar y flashear

Necesitas [PlatformIO](https://platformio.org/):

```bash
pio run -e XRP-T-Display-S3                 # compilar
pio run -e XRP-T-Display-S3 -t upload       # flashear por USB
pio device monitor                          # ver el log serie (115200)
```

Si no detecta la placa, mantén BOOT pulsado mientras la conectas para entrar en modo descarga.

## Estructura

```
src/
  main.cpp          setup/loop, botones y temporizadores
  wManager.cpp      portal WiFi (WiFiManager) y parámetros
  settings.cpp      carga/guardado de ajustes en SPIFFS
  priceService.cpp  tarea en segundo plano que consulta CoinGecko
  display.cpp       pantallas (TFT_eSPI + OpenFontRender)
  media/fonts.h     fuente NotoSans Bold embebida
lib/TFT_eSPI        librería de pantalla con la configuración de la T-Display S3
```
