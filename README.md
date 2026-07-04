# Apan Trikha's ML stack (ATML)

I am making this ML stack because I couldn't find a framework that can do the
following:

- **Must be lightweight, to implement language models on a PC.** Most industrial
grade frameworks while powerful, are bloated.
- **Must work with multiple accelerators.** The lightweight C++ frameworks tend
to be CPU only.
- **Must be flexible to rapidly prototype novel models in modern C++.** C++ is
controversial for a variety of reasons. Contrary to one of the most popular
opinion, you can write high level code in modern C++.

The core aim of the library is to **provide efficient, high level abstractions
to infer deep learning models in modern C++.** This includes both researchers
prototyping novel models, and developers supporting novel accelerators.

To stay consistent with modern C++, I'm implementing this library as a C++
module. For the implementation guide, please refer 
[Bjarne's 21st century C++ paper](https://www.stroustrup.com/21st-Century-C++.pdf).

## Dependency
Any C++ compiler supporting C++23. GCC and Clang will work out of the box. Use
`ninja` to scan for modules.

## Current state
Currently, this is work in progress. And I've just implemented the foundation
to implement tensors.

## Roadmap

- [ ] Tensor library
- [ ] Lazy evaluator for tensor ops
- [ ] Reverse-mode autodiff
- [ ] Kernel scheduling and fusion
- [ ] CPU codegen support
- [ ] Vulkan codegen support
- [ ] ROCm codegen support
