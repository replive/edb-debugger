/*
Copyright (C) 2015 - 2015 Evan Teran
                          evan.teran@gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "PlatformProcess.h"
#include "DebuggerCore.h"
#include "PlatformCommon.h"
#include "PlatformRegion.h"
#include "edb.h"
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <boost/functional/hash.hpp>
#include <fstream>
#include <sys/mman.h>

namespace DebuggerCore {
namespace {

#ifdef EDB_X86_64
#define EDB_WORDSIZE sizeof(quint64)
#elif defined(EDB_X86)
#define EDB_WORDSIZE sizeof(quint32)
#endif


//------------------------------------------------------------------------------
// Name: process_map_line
// Desc: parses the data from a line of a memory map file
//------------------------------------------------------------------------------
IRegion::pointer process_map_line(const QString &line) {

	edb::address_t start;
	edb::address_t end;
	edb::address_t base;
	IRegion::permissions_t permissions;
	QString name;

	const QStringList items = line.split(" ", QString::SkipEmptyParts);
	if(items.size() >= 3) {
		bool ok;
		const QStringList bounds = items[0].split("-");
		if(bounds.size() == 2) {
			start = edb::address_t::fromHexString(bounds[0],&ok);
			if(ok) {
				end = edb::address_t::fromHexString(bounds[1],&ok);
				if(ok) {
					base = edb::address_t::fromHexString(items[2],&ok);
					if(ok) {
						const QString perms = items[1];
						permissions = 0;
						if(perms[0] == 'r') permissions |= PROT_READ;
						if(perms[1] == 'w') permissions |= PROT_WRITE;
						if(perms[2] == 'x') permissions |= PROT_EXEC;

						if(items.size() >= 6) {
							name = items[5];
						}

						return std::make_shared<PlatformRegion>(start, end, base, name, permissions);
					}
				}
			}
		}
	}
	return nullptr;;
}

}

//------------------------------------------------------------------------------
// Name: PlatformProcess
// Desc: 
//------------------------------------------------------------------------------
PlatformProcess::PlatformProcess(DebuggerCore *core, edb::pid_t pid) : core_(core), pid_(pid) {

}

//------------------------------------------------------------------------------
// Name: ~PlatformProcess
// Desc: 
//------------------------------------------------------------------------------
PlatformProcess::~PlatformProcess() {
}

//------------------------------------------------------------------------------
// Name: read_pages
// Desc: reads <count> pages from the process starting at <address>
// Note: buf's size must be >= count * core_->page_size()
// Note: address should be page aligned.
//------------------------------------------------------------------------------
std::size_t PlatformProcess::read_pages(edb::address_t address, void *buf, std::size_t count) {

	Q_ASSERT(buf);

	return core_->read_pages(address,buf,count);
}

//------------------------------------------------------------------------------
// Name: read_bytes
// Desc: reads <len> bytes into <buf> starting at <address>
// Note: returns the number of bytes read <N>
// Note: if the read is short, only the first <N> bytes are defined
//------------------------------------------------------------------------------
std::size_t PlatformProcess::read_bytes(const edb::address_t address, void *buf, const std::size_t len) {

	Q_ASSERT(buf);

	if(len != 0) {
		return core_->read_bytes(address, buf, len);
	}

	return 0;
}

//------------------------------------------------------------------------------
// Name: write_bytes
// Desc: writes <len> bytes from <buf> starting at <address>
//------------------------------------------------------------------------------
std::size_t PlatformProcess::write_bytes(edb::address_t address, const void *buf, std::size_t len) {

	Q_ASSERT(buf);
	
	if(len != 0) {
		return core_->write_bytes(address, buf, len);
	}
	
	return 0;
}

//------------------------------------------------------------------------------
// Name: 
// Desc: 
//------------------------------------------------------------------------------
QDateTime PlatformProcess::start_time() const {
	QFileInfo info(QString("/proc/%1/stat").arg(pid_));
	return info.created();
}

//------------------------------------------------------------------------------
// Name: 
// Desc: 
//------------------------------------------------------------------------------
QList<QByteArray> PlatformProcess::arguments() const {
	QList<QByteArray> ret;

	if(pid_ != 0) {
		const QString command_line_file(QString("/proc/%1/cmdline").arg(pid_));
		QFile file(command_line_file);

		if(file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			QTextStream in(&file);

			QByteArray s;
			QChar ch;

			while(in.status() == QTextStream::Ok) {
				in >> ch;
				if(ch == '\0') {
					if(!s.isEmpty()) {
						ret << s;
					}
					s.clear();
				} else {
					s += ch;
				}
			}

			if(!s.isEmpty()) {
				ret << s;
			}
		}
	}
	return ret;
}

//------------------------------------------------------------------------------
// Name: 
// Desc: 
//------------------------------------------------------------------------------
QString PlatformProcess::current_working_directory() const {
	return edb::v1::symlink_target(QString("/proc/%1/cwd").arg(pid_));
}

//------------------------------------------------------------------------------
// Name: 
// Desc: 
//------------------------------------------------------------------------------
QString PlatformProcess::executable() const {
	return edb::v1::symlink_target(QString("/proc/%1/exe").arg(pid_));
}

//------------------------------------------------------------------------------
// Name: 
// Desc: 
//------------------------------------------------------------------------------
edb::pid_t PlatformProcess::pid() const {
	return pid_;
}

//------------------------------------------------------------------------------
// Name: 
// Desc: 
//------------------------------------------------------------------------------
IProcess::pointer PlatformProcess::parent() const {

	struct user_stat user_stat;
	int n = get_user_stat(pid_, &user_stat);
	if(n >= 4) {
		return std::make_shared<PlatformProcess>(core_, user_stat.ppid);
	}

	return nullptr;
}

//------------------------------------------------------------------------------
// Name:
// Desc:
//------------------------------------------------------------------------------
edb::address_t PlatformProcess::code_address() const {
	struct user_stat user_stat;
	int n = get_user_stat(pid(), &user_stat);
	if(n >= 26) {
		return user_stat.startcode;
	}
	return 0;
}

//------------------------------------------------------------------------------
// Name:
// Desc:
//------------------------------------------------------------------------------
edb::address_t PlatformProcess::data_address() const {
	struct user_stat user_stat;
	int n = get_user_stat(pid(), &user_stat);
	if(n >= 27) {
		return user_stat.endcode + 1; // endcode == startdata ?
	}
	return 0;
}

//------------------------------------------------------------------------------
// Name:
// Desc:
//------------------------------------------------------------------------------
QList<IRegion::pointer> PlatformProcess::regions() const {
	static QList<IRegion::pointer> regions;
	static size_t totalHash = 0;

	const QString map_file(QString("/proc/%1/maps").arg(pid_));

	// hash the region file to see if it changed or not
	{
		std::ifstream mf(map_file.toStdString());
		size_t newHash = 0;
		std::string line;
		
		while(std::getline(mf,line)) {
			boost::hash_combine(newHash, line);
		}
			
		if(totalHash == newHash) {
			return regions;
		}
		
		totalHash = newHash;
		regions.clear();
	}

	// it changed, so let's process it
	QFile file(map_file);
    if(file.open(QIODevice::ReadOnly | QIODevice::Text)) {

		QTextStream in(&file);
		QString line = in.readLine();

		while(!line.isNull()) {
			if(IRegion::pointer region = process_map_line(line)) {
				regions.push_back(region);
			}
			line = in.readLine();
		}
	}

	return regions;
}
}
