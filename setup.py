"""Mark wheels as platform-specific because the saved fast API uses DLL/PYD."""
from setuptools import Distribution, setup


class BinaryDistribution(Distribution):
    def has_ext_modules(self):
        return True


setup(distclass=BinaryDistribution)
