-- アプリストアの道具とインストールのテストに使う見本のアプリ(テトリス)。本物のアプリは kimu1109/pico-os-apps にある
local title = pico.tr("テトリス")
function render()
    pico.draw_text(8, 8, title, 0)
end
