{
  description = "EP Caravan - ESP32 PlatformIO development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;

      devShellFor = system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          python = pkgs.python3.withPackages (ps: [ ps.pyserial ]);
        in
        pkgs.mkShell {
          name = "ep-caravan-devshell";
          packages = [
            pkgs.platformio
            python
            pkgs.git
          ];

          shellHook = ''
            echo "EP Caravan devshell ready."
            echo "  pio run             -> build firmware"
            echo "  pio run -t upload   -> flash to board"
            echo "  pio device monitor  -> open serial console"
            echo "  python scripts/serial_reset.py -> reset board and capture boot output"
          '';
        };
    in
    {
      devShells = forAllSystems (system: {
        default = devShellFor system;
      });
    };
}
