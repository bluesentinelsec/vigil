# Vim Syntax Highlighting for BASL

## Install

Symlink (or copy) into your Vim runtime:

```sh
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
ln -sf "$(pwd)/syntax/basl.vim" ~/.vim/syntax/basl.vim
ln -sf "$(pwd)/ftdetect/basl.vim" ~/.vim/ftdetect/basl.vim
```

Then open any `.basl` file — highlighting is automatic.
