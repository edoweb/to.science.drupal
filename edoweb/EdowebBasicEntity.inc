<?php
/**
 * Copyright 2013 hbz NRW (http://www.hbz-nrw.de/)
 *
 * This file is part of regal-drupal.
 *
 * regal-drupal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * regal-drupal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regal-drupal.  If not, see <http://www.gnu.org/licenses/>.
 */

class EdowebBasicEntity extends Entity {

  private $__source = null;

  protected function defaultUri() {
    if (isset($this->remote_id)) {
      return array(
        'path' => "resource/{$this->remote_id}",
      );
    }
  }

  public function identifier() {
    $namespace = variable_get('edoweb_api_namespace');
    if (isset($this->remote_id)
        && ("$namespace:" === substr($this->remote_id, 0, strlen($namespace) + 1)
          || 'local:' == substr($this->remote_id, 0, 6))) {
      $remote_id = $this->remote_id;
    } else if (isset($this->remote_id)) {
      $remote_id = _edoweb_expand_curie($this->remote_id);
    } else {
      $remote_id = null;
    }
    return $remote_id;
  }

  public function internalIdentifier() {
    return isset($this->local_id)
      ? $this->local_id
      : null;
  }

  public function getSource() {
    return $this->__source;
  }

  public function setSource($source) {
    $this->__source = $source;
  }

}

