from setuptools import setup, Extension
from setuptools.command.build_py import build_py as _build_py
from setuptools.command.build_ext import build_ext as _build_ext


# Have to define a new class that is used for the build_py step because build_py expects to find
# asi.py, but that isn't generated until build_ext is run. Normally build_ext is run after
# build_py. When this class is used for build_py, build_ext is run first. See also the cmdclass
# option added to the setup() function further down in this file.
class build_py(_build_py):
    def run(self):
        self.run_command("build_ext")
        return super().run()


class build_ext(_build_ext):
    def run(self):
        # Normally include_dirs would be passed as an argument to the Extension object constructor
        # but we need to import numpy in order to find the include dirs and we don't know if numpy
        # is installed before setup() is called. Once we get to this point numpy should be
        # installed because it is listed in setup_requires for setup().
        from numpy.distutils.misc_util import get_numpy_include_dirs
        self.extensions[0].include_dirs=get_numpy_include_dirs()

        return super().run()


setup(
    name='asi',

    # Versions should comply with PEP440.  For a discussion on single-sourcing
    # the version across setup.py and the project code, see
    # https://packaging.python.org/en/latest/single_source_version.html
    version='0.1.0',

    description='Python wrapper for the ZWO ASI Camera Linux SDK',

    url='https://github.com/seeing-things/zwo/',

    author='Brett Gottula',
    author_email='bgottula@gmail.com',

    license='MIT',

    # See https://pypi.python.org/pypi?%3Aaction=list_classifiers
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'Topic :: Scientific/Engineering :: Astronomy',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3.6',
    ],

    keywords='astronomy telescopes zwo asi',

    python_requires='>=3.6',

    setup_requires=[
        'numpy',
    ],

    install_requires=[
        'numpy',
    ],

    ext_modules=[
        Extension(
            '_asi',
            ['asi.i'],
            swig_opts=['-I/usr/include'],
            libraries=['ASICamera2'],
        )
    ],

    py_modules=['asi'],

    cmdclass={'build_py': build_py, 'build_ext': build_ext},

    test_suite='asi_test',
)
