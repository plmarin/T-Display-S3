# Crypto Ticker

Firmware para la **LilyGo T-Display S3** que convierte la placa en un **visor del precio de XRP, XLM y VELO** siempre encendido.
Se conecta a tu WiFi, consulta la API pública de CoinGecko y muestra en la pantalla de 320x170 el precio actual,
la variación de las últimas 24 horas, una gráfica del día, velas diarias con volumen, el MACD diario, datos de mercado
y la hora local.
No hace falta clave de API ni ningún servidor propio: basta con la placa y un cable USB-C.

Basado en [NerdMiner v2](https://github.com/BitMaker-hub/NerdMiner_v2) (licencia MIT). Se ha eliminado todo lo
relativo al minado de BTC y se reutiliza el portal WiFi, el guardado de configuración y el manejo de pantalla y botones.
La caja 3D original de NerdMiner para esta placa sigue en [3d_files](3d_files/).

## Qué hace

- **Tres criptomonedas: XRP, XLM (Stellar) y VELO**, con una pantalla de **resumen** de las tres como pantalla
  principal. Con un doble clic en KEY se pasa del resumen a cada una de ellas.
- **Precio en tiempo casi real** en la moneda que elijas (`usd`, `eur`, `btc`...), refrescado cada 60 s por defecto.
- **Variación 24h** con flecha y color (verde si sube, rojo si baja), y **máximo/mínimo** del día.
- **Gráfica de las últimas 24 horas** con 48 velas de 30 minutos, coloreada según la tendencia.
- **Velas diarias de los últimos 30 días** con el volumen de cada día debajo.
- **MACD diario (12, 26, 9)** con línea MACD, línea de señal e histograma.
- **Datos de mercado**: capitalización, volumen 24h y hora del último dato.
- **Reloj** sincronizado por NTP, con cambio de horario automático.
- **Configuración desde el móvil** mediante un portal WiFi propio, sin recompilar.
- **Indicador de estado** que avisa si los datos están desactualizados o si se ha perdido la conexión.

## Cómo funciona

1. **Arranque**: enciende la pantalla y muestra una animación corta (~1,7 s): caen los logos de XRP, XLM y VELO,
   aparece el nombre y se dibuja una línea de cotización. Después carga los ajustes guardados en la memoria flash (SPIFFS).
2. **WiFi**: si no hay configuración (o se mantiene KEY pulsado), abre el portal `CryptoTickerAP` para elegir red y ajustes.
   Si no consigue conectarse, reinicia y vuelve a intentarlo.
3. **Hora**: sincroniza el reloj con `pool.ntp.org` / `time.google.com` usando la zona horaria configurada.
4. **Datos**: una tarea en segundo plano (core 0, junto a la pila WiFi) consulta CoinGecko por HTTPS de forma periódica
   y guarda el último resultado protegido por un mutex.
5. **Resumen y descarga en segundo plano**: al arrancar aparece el resumen, que se llena con los precios de las tres
   criptomonedas (una sola petición). Mientras tanto se descargan las gráficas de las tres, de una en una y con 15 s
   entre peticiones para no llegar al límite de la API. Si se elige una criptomoneda antes de que terminen sus gráficas,
   se ve su logo con una barra de progreso mientras se cargan en ese momento. Si algún dato no llega en 3 minutos, se
   muestran las pantallas igualmente y se sigue reintentando.
6. **Pantalla**: el bucle principal (core 1) lee una copia de esos datos y redibuja la pantalla activa cada segundo
   sobre un sprite en PSRAM, de modo que la interfaz nunca se bloquea esperando a la red.

## Hardware

- [LilyGo T-Display S3](https://www.lilygo.cc/products/t-display-s3): ESP32-S3 con 8 MB de PSRAM y pantalla ST7789
  de 1,9" (170x320) con bus paralelo de 8 bits.
- Botones integrados BOOT (GPIO0) y KEY (GPIO14). No hace falta ningún componente extra.
- Funciona por USB-C o con batería LiPo (el firmware activa el GPIO15 para alimentar la pantalla con batería).

## Pantallas

### Resumen (pantalla principal)

Una fila por criptomoneda con su logo, el precio, la variación 24h, la capitalización y una mini gráfica de las
últimas 24h (cuando ya se ha descargado). Abajo se ve el avance de la descarga de gráficas en segundo plano (`4/6`)
o, si la API ha llegado a su límite, la cuenta atrás para reintentar.

### Pantallas de cada criptomoneda

Con un doble clic en KEY se pasa del resumen a XRP, XLM y VELO, y de vuelta al resumen. Dentro de cada una, KEY pasa
de una pantalla a otra:

| # | Pantalla | Contenido |
|---|----------|-----------|
| 1 | Precio   | Precio actual en grande, variación 24h (verde/rojo), máximo y mínimo 24h |
| 2 | Gráfica  | Evolución de las últimas 24h (velas de 30 min) con el rango y el precio actual |
| 3 | Velas diarias | Velas japonesas de los últimos 30 días (UTC), barras de volumen diario, rango y variación del día |
| 4 | MACD diario | MACD (12, 26, 9) de los cierres diarios: línea MACD (azul), señal (ámbar), histograma y tendencia alcista/bajista |
| 5 | Reloj    | Hora y fecha locales, con precio y variación en la cabecera |
| 6 | Mercado  | Capitalización, volumen 24h, máx/mín, hora del último dato, señal WiFi e IP |

El punto de la esquina superior derecha indica el estado: **verde** datos al día, **ámbar** datos de hace más de 5 min,
**rojo** sin WiFi. Los puntos de la parte inferior indican la pantalla actual. La pantalla se queda fija
hasta que pulses KEY para pasar a la siguiente.

## Botones

| Botón | Acción |
|-------|--------|
| BOOT (GPIO0), pulsación | Apagar/encender pantalla |
| BOOT (GPIO0), doble pulsación | Girar la pantalla 180° |
| KEY (GPIO14), pulsación | Siguiente pantalla |
| KEY (GPIO14), doble pulsación | Siguiente vista: Resumen → XRP → XLM → VELO → Resumen |
| KEY (GPIO14), mantener 5 s | Borrar configuración y WiFi, y reiniciar |
| KEY (GPIO14), mantener al arrancar | Abrir el portal de configuración |

## Configuración

En el primer arranque (o manteniendo KEY al encender) la placa crea la red WiFi **`CryptoTickerAP`**
(clave **`CryptoTicker`**). Conéctate y abre `192.168.4.1` para elegir tu WiFi y estos ajustes:

| Ajuste | Por defecto | Notas |
|--------|-------------|-------|
| Moneda | `usd` | Cualquier `vs_currency` de CoinGecko: `usd`, `eur`, `gbp`, `jpy`, `btc`... |
| Refresco del precio | 60 s | Mínimo 30 s, por el límite de la API gratuita |
| Zona horaria | `CET-1CEST,M3.5.0,M10.5.0/3` | Formato POSIX TZ, con cambio de horario automático. Canarias: `WET0WEST,M3.5.0/1,M10.5.0` |

Los ajustes se guardan en SPIFFS (`/xrp_config.json`) y las credenciales WiFi las guarda WiFiManager.
Al encender siempre se empieza por el resumen.

## Datos

Se usa la API pública de [CoinGecko](https://www.coingecko.com/en/api), que no necesita clave. Los IDs de las
criptomonedas son `ripple` (XRP), `stellar` (XLM) y `velo` (VELO); en las rutas de abajo aparece `{id}`:

- `simple/price?ids=ripple,stellar,velo`: precio, variación 24h, capitalización y volumen de las tres a la vez,
  en cada refresco.
- `coins/{id}/ohlc?days=1`: 48 velas de 30 minutos para la gráfica y el máx/mín, cada 10 minutos.
- `coins/{id}/market_chart?days=60`: precios y volumen horarios de 60 días, cada 30 minutos. Se agrupan por día (UTC)
  para formar las velas diarias. El volumen de cada día es el volumen de 24h al final del día. El MACD se calcula con
  los 60 cierres para que las medias estén estabilizadas en los 30 días que se muestran.

Al arrancar se piden los precios de las tres criptomonedas en una sola petición, y después, en segundo plano y con
15 s de separación, las 6 gráficas (24h y diaria de cada una). Si se elige una criptomoneda cuyas gráficas aún no han
llegado, se piden en ese momento. Una vez descargadas, solo se refrescan las gráficas de la criptomoneda que está en
pantalla; en el resumen solo se refrescan los precios. Los datos de cada una se guardan en memoria, así que al cambiar
a una que ya se ha cargado aparece al momento.

Si la API responde `429` (límite de peticiones), se paran todas las peticiones durante el tiempo que indique la
cabecera `Retry-After` (entre 5 s y 10 min), o 2 minutos si no la envía. La pantalla de carga muestra la cuenta atrás.
Mientras tanto se puede volver con el doble clic a una criptomoneda ya cargada. Ante cualquier otro error se reintenta
a los 30 s.

## Compilar y flashear

Necesitas [PlatformIO](https://platformio.org/):

```bash
pio run -e Crypto-T-Display-S3                 # compilar
pio run -e Crypto-T-Display-S3 -t upload       # flashear por USB
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
  media/logos.h     logos de XRP, XLM y VELO (generado por tools/make_logos.py)
tools/make_logos.py descarga los logos de CoinGecko y genera media/logos.h (necesita Pillow)
platformio.ini      dependencias y configuración de TFT_eSPI para la T-Display S3 (pines, driver)
3d_files/           caja imprimible en 3D (de NerdMiner)
```

## Dependencias

Las descarga PlatformIO automáticamente:

- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI): driver de la pantalla
- [OpenFontRender](https://github.com/takkaO/OpenFontRender): texto con fuentes TrueType
- [ArduinoJson](https://arduinojson.org/): lectura de las respuestas de la API y de la configuración
- [WiFiManager](https://github.com/tzapu/WiFiManager): portal de configuración WiFi
- [OneButton](https://github.com/mathertel/OneButton): pulsaciones simples, dobles y largas

## Licencia

MIT, igual que el proyecto original. Ver [LICENSE](LICENSE).
