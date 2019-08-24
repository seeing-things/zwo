from setuptools import setup, find_packages, Extension
from setuptools.command.build_py import build_py as _build_py
# To use a consistent encoding
from codecs import open
from os import path

here = path.abspath(path.dirname(__file__))

# Get the long description from the README file
# with open(path.join(here, 'README.md'), encoding='utf-8') as f:
#     long_description = f.read()


# Have to define a new class that is used for the build_py step because build_py expects to find
# asi.py, but that isn't generated until build_ext is run. Normally build_ext is run after 
# build_py. When this class is used for build_py, build_ext is run first. See also the cmdclass
# option added to the setup() function further down in this file.
class build_py(_build_py):
    def run(self):
        self.run_command("build_ext")
        return super().run()

setup(
    name='asi',

    # Versions should comply with PEP440.  For a discussion on single-sourcing
    # the version across setup.py and the project code, see
    # https://packaging.python.org/en/latest/single_source_version.html
    version='0.1.0',

    description='Python wrapper for the ZWO ASI Camera Linux SDK',
    # long_description=long_description,

    # url='https://github.com/bgottula/point',

    author='Brett Gottula',
    author_email='bgottula@gmail.com',

    license='MIT',

    # See https://pypi.python.org/pypi?%3Aaction=list_classifiers
    classifiers=[
        'Development Status :: 3 - Alpha',
        'Intended Audience :: Developers',
        'Topic :: Scientific/Engineering :: Astronomy',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3',
    ],

    keywords='astronomy telescopes zwo asi',

    # packages=find_packages(exclude=['contrib', 'docs', 'tests']),

    ext_modules=[
        Extension(
            '_asi',
            ['asi.i'],
            swig_opts=['-modern', '-I../1.14.0715/linux_sdk/include/'],
            include_dirs=['../1.14.0715/linux_sdk/include/'],
            library_dirs=['../1.14.0715/linux_sdk/lib/x64/'],
            extra_objects=['../1.14.0715/linux_sdk/lib/x64/libASICamera2.so.1.14.0715'],
        )
    ],

    py_modules=['asi'],

    cmdclass={'build_py': build_py}
)
