%global pgmajorversion 17
%global pginstdir /usr/pgsql-%{pgmajorversion}
%global sname pgraft

Name:           %{sname}_%{pgmajorversion}
Version:        1.0.0
Release:        1%{?dist}
Summary:        Raft consensus extension for PostgreSQL %{pgmajorversion}
License:        PostgreSQL
URL:            https://github.com/pgelephant/pgraft
Source0:        %{sname}-%{version}.tar.gz

BuildRequires:  postgresql%{pgmajorversion}-devel
BuildRequires:  golang >= 1.18
BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  json-c-devel

Requires:       postgresql%{pgmajorversion}-server
Requires:       json-c

%description
pgraft is a PostgreSQL extension implementing Raft consensus for automatic
leader election, log replication, and split-brain prevention.

Features:
- Automatic leader election using Raft consensus
- Fast failover detection (2-second timeout)
- Split-brain prevention via quorum
- Key-value store with strong consistency
- Background worker for Raft processing
- Integration with pgbalancer for automatic failover

%prep
%setup -q -n %{sname}-%{version}

%build
cd src
export CGO_ENABLED=1
export GOCACHE=%{_builddir}/go-cache
mkdir -p ${GOCACHE}
go build -buildmode=c-shared -o pgraft_go.so pgraft_go.go
cd ..

export PATH=%{pginstdir}/bin:$PATH
make USE_PGXS=1

%install
rm -rf %{buildroot}
export PATH=%{pginstdir}/bin:$PATH
make USE_PGXS=1 DESTDIR=%{buildroot} install

install -d -m 700 %{buildroot}%{_sharedstatedir}/pgraft

%clean
rm -rf %{buildroot}

%post
echo "pgraft %{version} installed for PostgreSQL %{pgmajorversion}"
echo "Enable with: psql -c 'CREATE EXTENSION pgraft;'"

%files
%doc README.md
%license LICENSE
%{pginstdir}/lib/pgraft.so
%{pginstdir}/lib/pgraft_go.so
%{pginstdir}/share/extension/pgraft.control
%{pginstdir}/share/extension/pgraft--*.sql
%dir %attr(700,postgres,postgres) %{_sharedstatedir}/pgraft

%changelog
* Thu Oct 24 2024 pgElephant Team <team@pgelephant.org> - 1.0.0-1
- Initial release
- Raft consensus implementation
- Automatic leader election
- Fast failover (2s timeout)
- Integration with pgbalancer

