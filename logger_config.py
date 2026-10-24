import logging
from pathlib import Path


def configure_logging(log_dir: str = "logs", log_file: str = "lumo_edge.log") -> logging.Logger:
    Path(log_dir).mkdir(exist_ok=True, parents=True)
    logger = logging.getLogger("lumo_edge")
    logger.setLevel(logging.INFO)
    logger.propagate = False

    if not logger.handlers:
        handler = logging.FileHandler(Path(log_dir) / log_file, encoding="utf-8")
        formatter = logging.Formatter("%(asctime)s | %(levelname)s | %(name)s | %(message)s")
        handler.setFormatter(formatter)
        logger.addHandler(handler)

    return logger
