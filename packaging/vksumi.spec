Name:           vksumi
Version:        __VERSION__
Release:        1%{?dist}
Summary:        Vulkan layer for runtime color grading on Linux

License:        MIT
URL:            https://github.com/reakjra/vkSumi
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  glslang
BuildRequires:  gcc-c++
BuildRequires:  vulkan-loader-devel
BuildRequires:  libX11-devel
BuildRequires:  pkgconf-pkg-config

Requires:       vulkan-loader
Requires:       libX11

%description
vkSumi is an implicit Vulkan layer that exposes runtime color-grading sliders
(brightness, contrast, saturation, hue, gamma, RGB gain, 3-band lift/gain) via
a hot-reloadable .conf file. Per-game configs auto-create on first launch.
The Adrenalin / NVIDIA Freestyle equivalent that's been missing on Linux.

%prep
%autosetup -n vksumi-%{version}

%build
%meson
%meson_build

%install
%meson_install

%files
%license LICENSE
%doc README.md
%{_libdir}/libVkLayer_vksumi.so
%{_datadir}/vulkan/implicit_layer.d/vksumi.json
%{_bindir}/vksumi-toggle

%changelog
* Sat Apr 19 2026 vkSumi - 0.0.4-1
- initial spec
