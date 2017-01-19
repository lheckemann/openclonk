{ pkgs ? import <nixpkgs> {} , withEditor ? false }:

let
  version = "8.0-dev";

in
pkgs.stdenv.mkDerivation rec {
  name = "openclonk-${version}";

  src = builtins.filterSource (path: type: ! builtins.elem (baseNameOf path) [
    ".git" # leave out .git as it changes often in ways that do not affect the build
    "default.nix" # default.nix might change, but the only thing that matters is what it evaluates to, and nix takes care of that
    "result" # build result is irrelevant
  ]) ./.;

  enableParallelBuilding = true;

  buildInputs = with pkgs; [
    cmake SDL2 SDL2_mixer libjpeg libpng freetype glew tinyxml
  ] ++ stdenv.lib.optional withEditor qt5.full;

  postInstall = ''
    mkdir -p $out/bin
    ln -s $out/games/openclonk $out/bin/
  '';


  meta = with pkgs.stdenv.lib; {
    description = "A free multiplayer action game about mining, settling and fast-paced melees";
    homepage = "http://www.openclonk.org/";
    license = with licenses; [
      isc cc-by-sa-40
    ];
    maintainers = with lib.maintainers; [ lheckemann ];
  };
}
