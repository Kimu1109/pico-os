-- アプリストアの道具とインストールのテストに使う見本のアプリ(マインスイーパー)。本物のアプリは kimu1109/pico-os-apps にある
local title = pico.tr("マインスイーパー")
function render()
    pico.draw_text(8, 8, title, 0)
end
