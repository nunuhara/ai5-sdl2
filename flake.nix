{
  description = "Cross-platform implementation of elf's AI5WIN game engine";

  # Nixpkgs / NixOS version to use.
  inputs.nixpkgs.url = "nixpkgs/nixos-23.11";

  outputs = { self, nixpkgs }:
  let
    # System types to support.
    supportedSystems = [ "x86_64-linux" "x86_64-darwin" "aarch64-linux" "aarch64-darwin" ];

    # Helper function to generate an attrset '{ x86_64-linux = f "x86_64-linux"; ... }'.
    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

    # Nixpkgs instantiated for supported system types.
    nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; });

  in {

    # Set up ai5-sdl2 development environment:
    #     nix develop # create shell environment with dependencies available
    devShell = forAllSystems (system:
      let pkgs = nixpkgsFor.${system}; in with pkgs; pkgs.mkShell {
        nativeBuildInputs = [ meson ninja pkg-config xxd ];
        buildInputs = [
          SDL2
          SDL2_ttf
          ffmpeg
          libpng
          libsndfile
        ];
      }
    );

    # Build ai5-sdl2
    #     nix build .?submodules=1 # build ai5-sdl2 (outputs to ./result/)
    #     nix shell .?submodules=1 # create shell environment with ai5-sdl2 available
    # Install ai5-sdl2 to user profile:
    #     nix profile install .?submodules=1
    packages = forAllSystems (system:
      let pkgs = nixpkgsFor.${system}; in {
        default = with pkgs; stdenv.mkDerivation rec {
          name = "ai5-sdl2";
          src = ./.;
          mesonAutoFeatures = "auto";
          nativeBuildInputs = [ meson ninja pkg-config xxd ];
          buildInputs = [
            SDL2
            SDL2_ttf
            ffmpeg
            libpng
            libsndfile
          ];
        };
      }
    );
  };
}

