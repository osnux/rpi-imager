# 1. Qt 및 컴파일러 도구 PATH 설정 (버전에 맞게 경로 수정해 주어야 함. 확인!)
$QT_BASE = "C:\Qt"
$env:PATH = "$QT_BASE\Tools\CMake_64\bin;$QT_BASE\Tools\Ninja;$QT_BASE\Tools\mingw1310_64\bin;$QT_BASE\6.9.0\mingw_64\bin;" + $env:PATH

# 2. 빌드 폴더 생성 및 이동
cd c:\Users\wjmoh-home\Documents\work\rpi-imager
mkdir build -ErrorAction SilentlyContinue
cd build

# 3. CMake 설정 (반드시 소스 경로를 ../src 로 지정)
cmake -G "Ninja" `
  -DCMAKE_BUILD_TYPE=Release `
  -DQt6_ROOT="$QT_BASE\6.9.0\mingw_64" `
  -DMINGW64_ROOT="$QT_BASE\Tools\mingw1310_64" `
  -DIMAGER_SIGNED_APP=OFF `
  -DENABLE_INNO_INSTALLER=ON `
  ..\src

# 4. 컴파일 및 배포 패키징
cmake --build .

# 5. (선택 사항) 최종 Inno Setup 인스톨러 생성
cmake --build . --target inno_installer

cd c:\Users\wjmoh-home\Documents\work\rpi-imager\build
# 1. Inno Setup 컴파일러 경로를 전달하여 CMake 재구성
cmake -DENABLE_INNO_INSTALLER=ON -DINNO_COMPILER="$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" ..\src

# 2. inno_installer 타깃 빌드
cmake --build . --target inno_installer
