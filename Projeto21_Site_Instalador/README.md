# Projeto 21 — Site instalador

O diretório `site/` contém a página do instalador ESP Web Tools. O firmware precisa ser compilado e mesclado em `site/firmware/robo-expressivo-esp32c3.bin`.

## Publicação automática no GitHub Pages
1. Crie um repositório e envie todo o conteúdo desta pasta.
2. Em Settings > Pages, selecione **GitHub Actions** como Source.
3. Execute a action `Build firmware and deploy installer` ou faça push em `main`.
4. A Action instala PlatformIO, compila o ESP32-C3, gera o BIN mesclado e publica `site/` em HTTPS.
5. Abra a URL do GitHub Pages no Chrome/Edge e clique em **CONECTAR E INSTALAR**.

## Teste local
ESP Web Tools exige contexto seguro. `localhost` é aceito pelo navegador, mas o arquivo BIN precisa existir antes. Depois de compilar/mesclar o firmware, sirva a pasta `site` com um servidor local, por exemplo `python -m http.server 8000 --directory site`, e abra `http://localhost:8000`.

## Pinagem
- OLED SDA GPIO8
- OLED SCL GPIO9
- TTP223 GPIO7
- Buzzer GPIO5
- Vibração GPIO10
- ADC bateria GPIO3 (divisor 100k/100k)
