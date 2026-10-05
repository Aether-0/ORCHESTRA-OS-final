Name:           orchestra-os
Version:        %{_orchestra_version}
Release:        1
Summary:        Capability-tiered Linux scheduling research prototype
License:        MIT AND GPL-2.0-only
URL:            https://github.com/Aether-0/ORCHESTRA-OS-final
BuildArch:      %{_target_cpu}
AutoReqProv:    yes
Requires:       bash
Requires:       coreutils
Requires:       python3

%description
ORCHESTRA-OS provides a safe observer/control plane and an explicitly
opt-in, target-matched sched_ext research prototype. Installation never
builds or attaches a kernel scheduler.

%prep

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a %{_orchestra_payload}/. %{buildroot}/

%files
%defattr(-,root,root,-)
%dir /etc/orchestra-os
%config(noreplace) /etc/orchestra-os/*.json
/usr/bin/orchestra
/usr/lib/orchestra-os
/usr/lib/systemd/system/orchestra.service
/usr/share/doc/orchestra-os
/var/lib/orchestra-os

%changelog
* Sat Aug 29 2026 ORCHESTRA-OS contributors - %{_orchestra_version}-1
- Research-stable all-in-one package; kernel artifacts remain target-specific.
