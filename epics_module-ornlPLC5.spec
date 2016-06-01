%define _prefix __auto__
%define gemopt opt
%define name epics_module-ornlPLC5
%define version __auto__
%define release __auto__
%define repository gemini
%define debug_package %{nil}

Summary: %{name} Package, an application for EPICS base
Name: %{name}
Version: %{version}
Release: %{release}.%{?dist}.%{repository}
License: OSL
Group: Gemini
BuildRoot: /var/tmp/%{name}-%{version}-root
Source0: %{name}-%{version}.tar.gz
BuildArch: %{arch}
Prefix: %{_prefix}
BuildRequires: epics-base-devel%{?_isa} readline-devel%{?_isa}
Requires: epics-base%{?_isa}

%description
EPICS is a set of Open Source software tools, libraries and applications developed collaboratively and used worldwide to create distributed soft real-time control systems for scientific instruments such as a particle accelerators, telescopes and other large scientific experiments.
This is the application %{name}.

%prep
%setup -n %{name}

%build
make

%install
%define __os_install_post %{nil}
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/%{_prefix}/%{gemopt}/epics/modules/ornlPLC5
cp -r lib $RPM_BUILD_ROOT/%{_prefix}/%{gemopt}/epics/modules/ornlPLC5/
cp -r dbd $RPM_BUILD_ROOT/%{_prefix}/%{gemopt}/epics/modules/ornlPLC5/


%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root)
   /%{_prefix}/%{gemopt}/epics/modules/ornlPLC5/lib
   /%{_prefix}/%{gemopt}/epics/modules/ornlPLC5/dbd


%changelog

