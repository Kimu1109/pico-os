# PCデバッグ

### 1. 依存パッケージ
`sudo apt-get install libsdl2-dev`

### 2. ビルド
`cmake -S pc -B pc/build && cmake --build pc/build -j`

### 3. 実行
`./pc/build/picoos_pc          # マウス左ドラッグ = タッチ`