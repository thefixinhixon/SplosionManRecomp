// stfs_tool.cpp - list or extract an STFS package with the launcher's reader.
// Usage: stfs_tool <package> [destination folder]
#include "../src/stfs.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        out << "usage: stfs_tool <package> [destination folder]\n";
        return 2;
    }
    StfsPackage pkg;
    QString error;
    if (!pkg.open(args[1], &error)) {
        out << "error: " << error << "\n";
        return 1;
    }
    out << "title " << QString::number(pkg.titleId(), 16).toUpper() << " \"" << pkg.displayName()
        << "\" " << pkg.entries().size() << " entries, " << pkg.totalBytes() << " bytes\n";
    if (args.size() < 3) {
        for (const auto &e : pkg.entries())
            out << (e.isDirectory ? "d " : "f ") << e.size << " " << e.path << "\n";
        return 0;
    }
    if (!pkg.extractAll(args[2], nullptr, &error)) {
        out << "error: " << error << "\n";
        return 1;
    }
    out << "extracted\n";
    return 0;
}
