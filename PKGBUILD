pkgname=swordfish-git
pkgver=r1.abcdef0
pkgrel=1
pkgdesc="The better pkill: kill processes by name with more control."
arch=('x86_64')
url="https://github.com/seaslug/swordfish"
license=('MIT')
depends=()
makedepends=('git' 'gcc' 'make')
provides=('swordfish')
conflicts=('swordfish')
source=("git+$url")
md5sums=('SKIP')

pkgver() {
  cd "$srcdir/swordfish"
  # Automatically generate a version like r1234.abcd123
  echo "r$(git rev-list --count HEAD).$(git rev-parse --short HEAD)"
}

build() {
  cd "$srcdir/swordfish"
  make
}

package() {
  cd "$srcdir/swordfish"
  make DESTDIR="$pkgdir" PREFIX="/usr" install
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
  install -Dm644 README.md "$pkgdir/usr/share/doc/$pkgname/README.md"
}