# Ovládání krokového motoru NEMA 23 přes webové rozhraní (ESP32 + TMC2209)

Tento projekt umožňuje ovládání krokového motoru NEMA 23 pomocí driveru TMC2209, řízeného mikrokontrolérem ESP32. Ovládání probíhá přes jednoduché webové rozhraní dostupné na adrese http://192.168.4.1.

## Připojení k zařízení
Před otevřením webového rozhraní je třeba se připojit k WiFi síti, kterou ESP32 vytváří:
- SSID: ESP32
- Heslo: 12345678

## Poznámka
Pokud se webová stránka nenačítá nebo zobrazuje `Not found`, zkontrolujte, zda prohlížeč nehlásí „Připojení není zabezpečené“. V takovém případě potvrďte výjimku (např. možnost „Přejít i přesto“ nebo „Ignorovat varování“), abyste mohli pokračovat.


